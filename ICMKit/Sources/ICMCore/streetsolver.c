/*
 * Heads-up postflop solver: Discounted CFR on the public tree with vectors of
 * per-combo values. See streetsolver.h for the interface and DECISIONS.md for the
 * modelling choices.
 *
 * Layout
 *  - Each player has a list of live combos (weight > 0, not blocked by the
 *    starting board). Every vector below is indexed by that list.
 *  - Nodes live in one array; the children of a node are contiguous. Decision
 *    nodes own 2 * actions * combos floats: regrets, then the strategy sum.
 *  - Chance nodes store one child per suit-isomorphism class of the dealt card
 *    and a 52-entry map card -> (child slot, suit permutation).
 *  - Terminal payoffs depend only on what each player has put in, so they are
 *    computed once per distinct (investment 0, investment 1) and shared.
 *  - Showdowns use, for every possible final board, each player's live combos
 *    sorted by hand strength; a sweep with per-card sums handles card removal,
 *    so a showdown costs O(combos).
 */
#include "streetsolver.h"
#include "cards.h"
#include "handeval.h"
#include "icm.h"
#include "threadpool.h"

#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------------ */
/* Types                                                                    */

typedef struct {
    uint8_t kind;
    uint8_t player;      /* decision: actor; fold: folder */
    uint8_t nchild;
    uint8_t act;         /* action leading here */
    uint8_t card;        /* card dealt into this node (chance children) */
    uint8_t street_len;  /* board length on this node's street */
    uint8_t pad[2];
    int32_t child;       /* first child */
    int32_t util;        /* terminal payoff entry */
    float amount;        /* actor's street investment after the action */
    float pot;           /* chips in the middle at this node */
    int64_t data;        /* decision: float offset into store; chance: offset into cmaps */
} Node;

typedef struct {
    float u[2][3];       /* [player][win, lose, tie], payoff units, shifted by baseline */
    double key0, key1;
} Util;

typedef struct {
    uint16_t str;        /* hand strength */
    uint16_t idx;        /* live combo index */
    uint8_t a, b;        /* its two cards */
    int16_t same;        /* the same combo in the other player's list, or -1 */
} SortedCombo;

typedef struct {
    float *base;
    size_t cap, top;     /* in floats */
} Arena;

typedef struct {
    uint8_t dealt[2];
    int ndealt;
} Path;

/* WALK_BREV returns two vectors: the best-response value and the value under
   the average strategy (both against the opponent's average strategy). */
enum { WALK_CFR = 0, WALK_BREV };

#define UTIL_HASH 8192

struct ss_solver {
    ss_config cfg;
    int root_len;
    uint8_t root_board[5];

    /* live combos */
    int n[2];
    uint16_t *combo[2];
    uint8_t *c1[2], *c2[2];
    float *w[2];
    int16_t *same[2];        /* index of the same combo in the other player's list, or -1 */
    int16_t *live_of[2];     /* combo index -> live index or -1 (1326 entries) */

    /* tree */
    Node *nodes;
    size_t nnodes, cap_nodes;
    float *store;            /* float32 storage, or NULL when compact */
    uint16_t *cstore;        /* 16-bit storage (compact mode) */
    float *scales;           /* compact mode: 2 per decision node (regrets, strategy sums) */
    size_t nstore;           /* values per array set (regrets + strategy sums) */
    size_t ndecisions;
    int compact;
    uint8_t *cmaps;
    size_t ncmaps, cap_cmaps;
    Util *utils;
    int nutils, cap_utils;
    int32_t util_hash[UTIL_HASH];

    /* final boards */
    int nboards;
    int32_t *board_id;       /* 52 * 52 entries (see board_index) */
    int32_t *sb_off[2], *sb_cnt[2];
    SortedCombo *sb;
    size_t nsb;

    /* suit isomorphism */
    uint8_t perm[24][4];
    int perm_ok[24];         /* fixes the starting board and both ranges */
    int16_t *permc[2][24];   /* live index -> live index of the permuted combo */

    /* payoffs */
    double pot_value[2];
    double baseline[2];
    double norm;             /* sum over compatible combo pairs of w0 * w1 */

    /* runtime */
    int nworkers;
    Arena *arena;
    size_t scratch_floats;
    tp_pool *pool;
    int iter;
    int warm;                /* first iteration keeps the regrets (warm start) */
    float disc_pos, disc_neg, disc_strat;
    atomic_int cancel;
    atomic_int progress_iter;
    _Atomic double progress_expl;
    size_t mem_cur, mem_peak;
    double last_expl[2], last_expl_pct[2], last_value[2];
    int have_expl;
};

/* ------------------------------------------------------------------------ */
/* Memory accounting                                                        */

static void *xcalloc(ss_solver *S, size_t count, size_t size)
{
    if (count == 0) count = 1;
    void *p = calloc(count, size);
    if (p) {
        S->mem_cur += count * size;
        if (S->mem_cur > S->mem_peak) S->mem_peak = S->mem_cur;
    }
    return p;
}

static void xfree(ss_solver *S, void *p, size_t count, size_t size)
{
    if (!p) return;
    if (count == 0) count = 1;
    S->mem_cur -= count * size;
    free(p);
}

static int xgrow(ss_solver *S, void **p, size_t *cap, size_t need, size_t elem)
{
    if (need <= *cap) return 0;
    size_t nc = *cap ? *cap : 1024;
    while (nc < need) nc *= 2;
    void *q = realloc(*p, nc * elem);
    if (!q) return -1;
    memset((char *)q + *cap * elem, 0, (nc - *cap) * elem);
    S->mem_cur += (nc - *cap) * elem;
    if (S->mem_cur > S->mem_peak) S->mem_peak = S->mem_cur;
    *p = q;
    *cap = nc;
    return 0;
}

static void set_err(char *err, size_t len, const char *fmt, ...)
{
    if (!err || !len) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, len, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------------ */
/* Scratch arenas (one per worker thread)                                   */

static float *arena_alloc(Arena *a, size_t n)
{
    size_t need = (n + 15) & ~(size_t)15;
    if (a->top + need > a->cap) {
        fprintf(stderr, "streetsolver: scratch arena overflow (%zu + %zu > %zu)\n", a->top, need, a->cap);
        abort();
    }
    float *p = a->base + a->top;
    a->top += need;
    return p;
}

/* ------------------------------------------------------------------------ */
/* Configuration                                                            */

void ss_config_default(ss_config *c)
{
    memset(c, 0, sizeof(*c));
    c->num_bet_sizes = 2;
    c->bet_sizes[0] = 0.33;
    c->bet_sizes[1] = 0.75;
    c->allow_allin = 1;
    c->raise_multiple = 3.0;
    c->max_raises = 1;
    c->allin_threshold = 0.67;
    c->mode = SS_MODE_CHIPS;
    c->seat[0] = 0;
    c->seat[1] = 1;
    c->max_iterations = 1000;
    c->target_exploitability_pct = 0.5;
    c->check_every = 10;
    c->dcfr_alpha = 1.5;
    c->dcfr_beta = 0.0;
    c->dcfr_gamma = 2.0;
    c->threads = 1;
    c->use_isomorphism = 1;
}

/* ------------------------------------------------------------------------ */
/* Payoffs                                                                  */

static void outcome_values(const ss_solver *S, double ti0, double ti1, int winner, double out[2])
{
    const ss_config *c = &S->cfg;
    double total = c->pot + ti0 + ti1;
    double share0 = winner == 0 ? 1.0 : winner == 1 ? 0.0 : 0.5;
    double share1 = 1.0 - share0;
    if (c->mode == SS_MODE_CHIPS) {
        out[0] = share0 * total - ti0;
        out[1] = share1 * total - ti1;
        return;
    }
    double st[ICM_MAX_PLAYERS], eq[ICM_MAX_PLAYERS];
    for (int i = 0; i < c->icm_players; i++) st[i] = c->icm_stacks[i];
    st[c->seat[0]] = c->icm_stacks[c->seat[0]] - ti0 + share0 * total;
    st[c->seat[1]] = c->icm_stacks[c->seat[1]] - ti1 + share1 * total;
    for (int i = 0; i < c->icm_players; i++) if (st[i] < 0) st[i] = 0;   /* rounding guard */
    icm_equity(c->icm_players, st, c->icm_num_payouts, c->icm_payouts, eq);
    out[0] = eq[c->seat[0]];
    out[1] = eq[c->seat[1]];
}

static int util_get(ss_solver *S, double ti0, double ti1)
{
    long long k0 = llround(ti0 * 65536.0), k1 = llround(ti1 * 65536.0);
    unsigned h = (unsigned)((k0 * 1000003LL) ^ (k1 * 999331LL)) & (UTIL_HASH - 1);
    for (;;) {
        int32_t e = S->util_hash[h];
        if (e < 0) break;
        if (llround(S->utils[e].key0 * 65536.0) == k0 && llround(S->utils[e].key1 * 65536.0) == k1) return e;
        h = (h + 1) & (UTIL_HASH - 1);
    }
    if (S->nutils >= UTIL_HASH / 2) return -1;
    if (S->nutils >= S->cap_utils) {
        size_t cap = (size_t)S->cap_utils;
        if (xgrow(S, (void **)&S->utils, &cap, (size_t)S->nutils + 1, sizeof(Util))) return -1;
        S->cap_utils = (int)cap;
    }
    Util *u = &S->utils[S->nutils];
    u->key0 = ti0;
    u->key1 = ti1;
    double v[3][2];
    outcome_values(S, ti0, ti1, 0, v[0]);
    outcome_values(S, ti0, ti1, 1, v[1]);
    outcome_values(S, ti0, ti1, 2, v[2]);
    for (int p = 0; p < 2; p++) {
        u->u[p][0] = (float)(v[p][p] - S->baseline[p]);          /* p wins */
        u->u[p][1] = (float)(v[1 - p][p] - S->baseline[p]);      /* other wins */
        u->u[p][2] = (float)(v[2][p] - S->baseline[p]);
    }
    S->util_hash[h] = S->nutils;
    return S->nutils++;
}

/* ------------------------------------------------------------------------ */
/* Suit permutations                                                        */

static int perm_card(const uint8_t *perm, int card)
{
    return make_card(card_rank(card), perm[card_suit(card)]);
}

static void init_perms(ss_solver *S)
{
    int k = 0;
    for (int a = 0; a < 4; a++)
        for (int b = 0; b < 4; b++)
            for (int c = 0; c < 4; c++)
                for (int d = 0; d < 4; d++) {
                    if (a == b || a == c || a == d || b == c || b == d || c == d) continue;
                    S->perm[k][0] = (uint8_t)a; S->perm[k][1] = (uint8_t)b;
                    S->perm[k][2] = (uint8_t)c; S->perm[k][3] = (uint8_t)d;
                    k++;
                }
    /* k == 24 and perm[0] is the identity */
}

static int board_fixed(const uint8_t *perm, const uint8_t *board, int len)
{
    for (int i = 0; i < len; i++) {
        int img = perm_card(perm, board[i]), found = 0;
        for (int j = 0; j < len; j++) found |= board[j] == img;
        if (!found) return 0;
    }
    return 1;
}

static int setup_isomorphism(ss_solver *S)
{
    init_perms(S);
    for (int k = 0; k < 24; k++) {
        const uint8_t *pm = S->perm[k];
        int ok = k == 0 || (S->cfg.use_isomorphism && board_fixed(pm, S->root_board, S->root_len));
        for (int p = 0; ok && p < 2; p++)
            for (int ci = 0; ci < NUM_COMBOS && ok; ci++) {
                int hi, lo;
                combo_cards(ci, &hi, &lo);
                int img = combo_index(perm_card(pm, hi), perm_card(pm, lo));
                if (S->cfg.range[p][ci] != S->cfg.range[p][img]) ok = 0;
            }
        S->perm_ok[k] = ok;
        if (!ok) continue;
        for (int p = 0; p < 2; p++) {
            S->permc[p][k] = xcalloc(S, (size_t)S->n[p], sizeof(int16_t));
            if (!S->permc[p][k]) return -1;
            for (int i = 0; i < S->n[p]; i++) {
                int img = combo_index(perm_card(pm, S->c1[p][i]), perm_card(pm, S->c2[p][i]));
                int li = S->live_of[p][img];
                if (li < 0) return -1;   /* cannot happen: range and board are invariant */
                S->permc[p][k][i] = (int16_t)li;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Final boards and sorted strengths                                        */

/* Board id of the starting board plus the dealt cards (all five cards known). */
static int board_index(const ss_solver *S, const uint8_t *dealt, int ndealt)
{
    if (S->root_len == 5) return 0;
    if (S->root_len == 4) return S->board_id[dealt[0] * 52 + dealt[0]];
    (void)ndealt;
    int a = dealt[0], b = dealt[1];
    return S->board_id[a < b ? a * 52 + b : b * 52 + a];
}

static int cmp_sorted(const void *x, const void *y)
{
    const SortedCombo *a = x, *b = y;
    return (int)a->str - (int)b->str;
}

static int setup_boards(ss_solver *S)
{
    S->board_id = xcalloc(S, 52 * 52, sizeof(int32_t));
    if (!S->board_id) return -1;
    for (int i = 0; i < 52 * 52; i++) S->board_id[i] = -1;
    uint8_t on_board[52] = { 0 };
    for (int i = 0; i < S->root_len; i++) on_board[S->root_board[i]] = 1;

    int nb = 0;
    if (S->root_len == 5) nb = 1;
    else if (S->root_len == 4) {
        for (int r = 0; r < 52; r++) if (!on_board[r]) S->board_id[r * 52 + r] = nb++;
    } else {
        for (int a = 0; a < 52; a++)
            for (int b = a + 1; b < 52; b++)
                if (!on_board[a] && !on_board[b]) S->board_id[a * 52 + b] = nb++;
    }
    S->nboards = nb;
    for (int p = 0; p < 2; p++) {
        S->sb_off[p] = xcalloc(S, (size_t)nb, sizeof(int32_t));
        S->sb_cnt[p] = xcalloc(S, (size_t)nb, sizeof(int32_t));
        if (!S->sb_off[p] || !S->sb_cnt[p]) return -1;
    }
    S->nsb = (size_t)nb * (size_t)(S->n[0] + S->n[1]);
    S->sb = xcalloc(S, S->nsb, sizeof(SortedCombo));
    if (!S->sb) return -1;

    size_t pos = 0;
    for (int a = 0; a < 52; a++)
        for (int b = a; b < 52; b++) {
            int id;
            uint8_t cards[7];
            int nc = S->root_len;
            memcpy(cards, S->root_board, (size_t)S->root_len);
            if (S->root_len == 5) {
                if (a != 0 || b != 0) continue;
                id = 0;
            } else if (S->root_len == 4) {
                if (a != b || (id = S->board_id[a * 52 + a]) < 0) continue;
                cards[nc++] = (uint8_t)a;
            } else {
                if (a == b || (id = S->board_id[a * 52 + b]) < 0) continue;
                cards[nc++] = (uint8_t)a;
                cards[nc++] = (uint8_t)b;
            }
            for (int p = 0; p < 2; p++) {
                S->sb_off[p][id] = (int32_t)pos;
                int cnt = 0;
                for (int i = 0; i < S->n[p]; i++) {
                    int x = S->c1[p][i], y = S->c2[p][i];
                    int blocked = 0;
                    for (int k = S->root_len; k < 5; k++) blocked |= cards[k] == x || cards[k] == y;
                    if (blocked) continue;
                    cards[5] = (uint8_t)x;
                    cards[6] = (uint8_t)y;
                    S->sb[pos + cnt].str = handeval7(cards);
                    S->sb[pos + cnt].idx = (uint16_t)i;
                    S->sb[pos + cnt].a = (uint8_t)x;
                    S->sb[pos + cnt].b = (uint8_t)y;
                    S->sb[pos + cnt].same = S->same[p][i];
                    cnt++;
                }
                qsort(S->sb + pos, (size_t)cnt, sizeof(SortedCombo), cmp_sorted);
                S->sb_cnt[p][id] = cnt;
                pos += (size_t)cnt;
            }
        }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Tree building                                                            */

typedef struct {
    int street_len;
    double pot_start;     /* pot at the start of the street */
    double stack;         /* each player's stack at the start of the street */
    double inv[2];        /* put in on this street */
    double ti[2];         /* put in on earlier streets of this solve */
    int to_act;
    int raises;
    Path path;
} BuildState;

static int64_t new_nodes(ss_solver *S, int count)
{
    size_t first = S->nnodes;
    if (xgrow(S, (void **)&S->nodes, &S->cap_nodes, first + (size_t)count, sizeof(Node))) return -1;
    S->nnodes += (size_t)count;
    return (int64_t)first;
}

static int build_decision(ss_solver *S, int64_t idx, const BuildState *bs);

static int make_terminal(ss_solver *S, int64_t idx, int kind, int folder, double ti0, double ti1)
{
    int u = util_get(S, ti0, ti1);
    if (u < 0) return -1;
    Node *nd = &S->nodes[idx];
    nd->kind = (uint8_t)kind;
    nd->player = (uint8_t)folder;
    nd->util = u;
    return 0;
}

static int build_chance(ss_solver *S, int64_t idx, const BuildState *next)
{
    /* next describes the street after the card; next->path does not yet hold it. */
    const Path *path = &next->path;
    uint8_t on_board[52] = { 0 };
    for (int i = 0; i < S->root_len; i++) on_board[S->root_board[i]] = 1;
    for (int i = 0; i < path->ndealt; i++) on_board[path->dealt[i]] = 1;

    int valid[24], nvalid = 0;
    for (int k = 0; k < 24; k++) {
        if (!S->perm_ok[k]) continue;
        int ok = 1;
        for (int i = 0; i < path->ndealt; i++) ok &= perm_card(S->perm[k], path->dealt[i]) == path->dealt[i];
        if (ok) valid[nvalid++] = k;
    }

    size_t moff = S->ncmaps;
    if (xgrow(S, (void **)&S->cmaps, &S->cap_cmaps, moff + 104, 1)) return -1;
    S->ncmaps += 104;
    uint8_t rep_of[52], perm_of[52];
    int slot_of_rep[52];
    for (int c = 0; c < 52; c++) slot_of_rep[c] = -1;
    int nslots = 0;
    uint8_t slot_card[52];
    for (int c = 0; c < 52; c++) {
        if (on_board[c]) { rep_of[c] = CARD_NONE; continue; }
        int best = c, bp = 0;
        for (int v = 0; v < nvalid; v++) {
            int img = perm_card(S->perm[valid[v]], c);
            if (img < best) { best = img; bp = valid[v]; }
        }
        rep_of[c] = (uint8_t)best;
        perm_of[c] = (uint8_t)bp;
    }
    for (int c = 0; c < 52; c++)
        if (rep_of[c] != CARD_NONE && slot_of_rep[rep_of[c]] < 0) {
            slot_of_rep[rep_of[c]] = nslots;
            slot_card[nslots++] = rep_of[c];
        }
    for (int c = 0; c < 52; c++) {
        S->cmaps[moff + 2 * c] = rep_of[c] == CARD_NONE ? 255 : (uint8_t)slot_of_rep[rep_of[c]];
        S->cmaps[moff + 2 * c + 1] = rep_of[c] == CARD_NONE ? 0 : perm_of[c];
    }

    int64_t first = new_nodes(S, nslots);
    if (first < 0) return -1;
    Node *nd = &S->nodes[idx];
    nd->kind = SS_NODE_CHANCE;
    nd->nchild = (uint8_t)nslots;
    nd->child = (int32_t)first;
    nd->data = (int64_t)moff;
    for (int s = 0; s < nslots; s++) {
        BuildState cs = *next;
        cs.path.dealt[cs.path.ndealt++] = slot_card[s];
        Node *ch = &S->nodes[first + s];
        ch->act = SS_ACT_DEAL;
        ch->card = slot_card[s];
        ch->pot = (float)(cs.pot_start);
        if (build_decision(S, first + s, &cs)) return -1;
    }
    return 0;
}

/* The street ended with both players having put in the same amount. */
static int end_street(ss_solver *S, int64_t idx, const BuildState *bs)
{
    double ti0 = bs->ti[0] + bs->inv[0], ti1 = bs->ti[1] + bs->inv[1];
    if (bs->street_len == 5) return make_terminal(S, idx, SS_NODE_SHOWDOWN, 0, ti0, ti1);
    BuildState nx = *bs;
    nx.street_len = bs->street_len + 1;
    nx.pot_start = bs->pot_start + bs->inv[0] + bs->inv[1];
    nx.stack = bs->stack - bs->inv[0];
    nx.inv[0] = nx.inv[1] = 0.0;
    nx.ti[0] = ti0;
    nx.ti[1] = ti1;
    nx.to_act = 0;
    nx.raises = 0;
    return build_chance(S, idx, &nx);
}

typedef struct { int act; double amount; } Action;

static void add_action(Action *list, int *n, int act, double amount, double stack)
{
    if (amount >= stack - 1e-9) { act = SS_ACT_ALLIN; amount = stack; }
    for (int i = 0; i < *n; i++)
        if (fabs(list[i].amount - amount) < 1e-9 && list[i].act != SS_ACT_FOLD && list[i].act != SS_ACT_CHECK &&
            list[i].act != SS_ACT_CALL) return;
    list[*n].act = act;
    list[*n].amount = amount;
    (*n)++;
}

/* Converts a bet or raise to all-in when it leaves less than the threshold behind. */
static double sized(const ss_config *c, double to, double stack, double pot_start)
{
    if (to >= stack) return stack;
    double behind = stack - to;
    double pot_after_call = pot_start + 2.0 * to;
    if (behind < c->allin_threshold * pot_after_call) return stack;
    return to;
}

static int cmp_action(const void *x, const void *y)
{
    const Action *a = x, *b = y;
    if (a->act == SS_ACT_FOLD || b->act == SS_ACT_FOLD) return (b->act == SS_ACT_FOLD) - (a->act == SS_ACT_FOLD);
    return (a->amount > b->amount) - (a->amount < b->amount);
}

static int build_decision(ss_solver *S, int64_t idx, const BuildState *bs)
{
    const ss_config *c = &S->cfg;
    int me = bs->to_act, other = 1 - me;
    double facing = bs->inv[other], mine = bs->inv[me];
    double pot_now = bs->pot_start + bs->inv[0] + bs->inv[1];
    Action acts[16];
    int na = 0;

    if (facing <= mine + 1e-12) {
        acts[na].act = SS_ACT_CHECK; acts[na].amount = mine; na++;
        for (int i = 0; i < c->num_bet_sizes; i++)
            add_action(acts, &na, SS_ACT_BET, sized(c, c->bet_sizes[i] * pot_now, bs->stack, bs->pot_start), bs->stack);
        if (c->allow_allin) add_action(acts, &na, SS_ACT_ALLIN, bs->stack, bs->stack);
    } else {
        acts[na].act = SS_ACT_FOLD; acts[na].amount = mine; na++;
        acts[na].act = SS_ACT_CALL; acts[na].amount = facing; na++;
        if (facing < bs->stack - 1e-9) {
            if (bs->raises < c->max_raises)
                add_action(acts, &na, SS_ACT_RAISE, sized(c, c->raise_multiple * facing, bs->stack, bs->pot_start), bs->stack);
            if (c->allow_allin) add_action(acts, &na, SS_ACT_ALLIN, bs->stack, bs->stack);
        }
    }
    qsort(acts, (size_t)na, sizeof(Action), cmp_action);

    int me_n = S->n[me];
    int64_t first = new_nodes(S, na);
    if (first < 0) return -1;
    {
        Node *nd = &S->nodes[idx];
        nd->kind = SS_NODE_DECISION;
        nd->player = (uint8_t)me;
        nd->nchild = (uint8_t)na;
        nd->child = (int32_t)first;
        nd->street_len = (uint8_t)bs->street_len;
        nd->data = (int64_t)S->nstore;
        nd->util = (int32_t)S->ndecisions++;   /* decision ordinal (scales in compact mode) */
        if (!nd->pot) nd->pot = (float)pot_now;
    }
    S->nstore += 2 * (size_t)na * (size_t)me_n;

    for (int a = 0; a < na; a++) {
        int64_t ci = first + a;
        BuildState nx = *bs;
        nx.inv[me] = acts[a].amount;
        {
            Node *ch = &S->nodes[ci];
            ch->act = (uint8_t)acts[a].act;
            ch->amount = (float)acts[a].amount;
            ch->street_len = (uint8_t)bs->street_len;
            ch->pot = (float)(bs->pot_start + nx.inv[0] + nx.inv[1]);
        }
        int r;
        switch (acts[a].act) {
        case SS_ACT_FOLD:
            r = make_terminal(S, ci, SS_NODE_FOLD, me, bs->ti[0] + bs->inv[0], bs->ti[1] + bs->inv[1]);
            break;
        case SS_ACT_CHECK:
            if (me == 1) r = end_street(S, ci, &nx);
            else { nx.to_act = other; r = build_decision(S, ci, &nx); }
            break;
        case SS_ACT_CALL:
            if (facing >= bs->stack - 1e-9)
                r = make_terminal(S, ci, SS_NODE_SHOWDOWN, 0, bs->ti[0] + nx.inv[0], bs->ti[1] + nx.inv[1]);
            else r = end_street(S, ci, &nx);
            break;
        default:   /* bet, raise, all-in */
            nx.to_act = other;
            if (facing > mine + 1e-12) nx.raises++;
            r = build_decision(S, ci, &nx);
            break;
        }
        if (r) return -1;
    }
    return 0;
}

/* Upper bound of scratch floats a walk from this node needs. */
static size_t scratch_need(const ss_solver *S, int64_t idx, size_t N)
{
    const Node *nd = &S->nodes[idx];
    size_t best = 0;
    if (nd->kind == SS_NODE_DECISION) {
        for (int a = 0; a < nd->nchild; a++) {
            size_t x = scratch_need(S, nd->child + a, N);
            if (x > best) best = x;
        }
        return best + (2 * (size_t)nd->nchild + 3) * (N + 16);
    }
    if (nd->kind == SS_NODE_CHANCE) {
        for (int a = 0; a < nd->nchild; a++) {
            size_t x = scratch_need(S, nd->child + a, N);
            if (x > best) best = x;
        }
        return best + ((size_t)nd->nchild + 3) * (N + 16);
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Creation                                                                 */

static int setup_ranges(ss_solver *S)
{
    uint8_t on_board[52] = { 0 };
    for (int i = 0; i < S->root_len; i++) on_board[S->root_board[i]] = 1;
    for (int p = 0; p < 2; p++) {
        int cnt = 0;
        for (int ci = 0; ci < NUM_COMBOS; ci++) {
            int hi, lo;
            combo_cards(ci, &hi, &lo);
            float wt = S->cfg.range[p][ci];
            if (!(wt >= 0.0f) || wt > 1.0f) return -1;
            if (wt > 0.0f && !on_board[hi] && !on_board[lo]) cnt++;
        }
        if (cnt == 0) return -2;
        S->n[p] = cnt;
        S->combo[p] = xcalloc(S, (size_t)cnt, sizeof(uint16_t));
        S->c1[p] = xcalloc(S, (size_t)cnt, 1);
        S->c2[p] = xcalloc(S, (size_t)cnt, 1);
        S->w[p] = xcalloc(S, (size_t)cnt, sizeof(float));
        S->same[p] = xcalloc(S, (size_t)cnt, sizeof(int16_t));
        S->live_of[p] = xcalloc(S, NUM_COMBOS, sizeof(int16_t));
        if (!S->combo[p] || !S->c1[p] || !S->c2[p] || !S->w[p] || !S->same[p] || !S->live_of[p]) return -1;
        int k = 0;
        for (int ci = 0; ci < NUM_COMBOS; ci++) {
            int hi, lo;
            combo_cards(ci, &hi, &lo);
            float wt = S->cfg.range[p][ci];
            S->live_of[p][ci] = -1;
            if (wt > 0.0f && !on_board[hi] && !on_board[lo]) {
                S->combo[p][k] = (uint16_t)ci;
                S->c1[p][k] = (uint8_t)hi;
                S->c2[p][k] = (uint8_t)lo;
                S->w[p][k] = wt;
                S->live_of[p][ci] = (int16_t)k;
                k++;
            }
        }
    }
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < S->n[p]; i++) S->same[p][i] = S->live_of[1 - p][S->combo[p][i]];
    return 0;
}

/* Sum over opponent combos compatible with each own combo of reach (fold-style). */
static void compat_mass(const ss_solver *S, int me, const float *reach, float *out)
{
    int op = 1 - me;
    float cs[52] = { 0 }, tot = 0.0f;
    const uint8_t *a = S->c1[op], *b = S->c2[op];
    for (int v = 0; v < S->n[op]; v++) {
        float r = reach[v];
        tot += r;
        cs[a[v]] += r;
        cs[b[v]] += r;
    }
    const uint8_t *x = S->c1[me], *y = S->c2[me];
    const int16_t *sm = S->same[me];
    for (int h = 0; h < S->n[me]; h++) {
        float m = tot - cs[x[h]] - cs[y[h]];
        if (sm[h] >= 0) m += reach[sm[h]];
        out[h] = m;
    }
}

ss_solver *ss_create(const ss_config *cfg, char *err, size_t err_len)
{
    if (!cfg || !cfg->range[0] || !cfg->range[1]) { set_err(err, err_len, "missing ranges"); return NULL; }
    if (cfg->board_len < 3 || cfg->board_len > 5) { set_err(err, err_len, "board must have 3, 4 or 5 cards"); return NULL; }
    for (int i = 0; i < cfg->board_len; i++) {
        if (cfg->board[i] >= 52) { set_err(err, err_len, "bad board card"); return NULL; }
        for (int j = 0; j < i; j++)
            if (cfg->board[i] == cfg->board[j]) { set_err(err, err_len, "duplicate board card"); return NULL; }
    }
    if (!(cfg->pot > 0) || !(cfg->stack >= 0)) { set_err(err, err_len, "pot must be > 0 and stack >= 0"); return NULL; }
    if (cfg->num_bet_sizes < 0 || cfg->num_bet_sizes > SS_MAX_BET_SIZES) { set_err(err, err_len, "bad bet size count"); return NULL; }
    for (int i = 0; i < cfg->num_bet_sizes; i++)
        if (!(cfg->bet_sizes[i] > 0)) { set_err(err, err_len, "bet sizes must be > 0"); return NULL; }
    if (!(cfg->raise_multiple > 1.0)) { set_err(err, err_len, "raise multiple must be > 1"); return NULL; }
    if (cfg->mode == SS_MODE_ICM) {
        if (cfg->icm_players < 2 || cfg->icm_players > SS_MAX_PLAYERS || cfg->seat[0] == cfg->seat[1] ||
            cfg->seat[0] < 0 || cfg->seat[1] < 0 || cfg->seat[0] >= cfg->icm_players || cfg->seat[1] >= cfg->icm_players ||
            cfg->icm_num_payouts < 1 || cfg->icm_num_payouts > SS_MAX_PLAYERS) {
            set_err(err, err_len, "bad ICM table");
            return NULL;
        }
        for (int i = 0; i < cfg->icm_players; i++)
            if (!(cfg->icm_stacks[i] >= 0)) { set_err(err, err_len, "bad ICM stack"); return NULL; }
        if (cfg->icm_stacks[cfg->seat[0]] < cfg->stack - 1e-9 || cfg->icm_stacks[cfg->seat[1]] < cfg->stack - 1e-9) {
            set_err(err, err_len, "ICM stacks of the two players must cover the effective stack");
            return NULL;
        }
    } else if (cfg->mode != SS_MODE_CHIPS) { set_err(err, err_len, "bad mode"); return NULL; }

    handeval_init();
    ss_solver *S = calloc(1, sizeof(*S));
    if (!S) { set_err(err, err_len, "out of memory"); return NULL; }
    S->mem_cur = S->mem_peak = sizeof(*S);
    S->cfg = *cfg;
    if (S->cfg.check_every < 1) S->cfg.check_every = 1;
    if (S->cfg.max_iterations < 1) S->cfg.max_iterations = 1;
    if (S->cfg.threads < 1) S->cfg.threads = 1;
    S->cfg.range[0] = S->cfg.range[1] = NULL;   /* not kept: copied into the live lists */
    S->root_len = cfg->board_len;
    memcpy(S->root_board, cfg->board, 5);
    atomic_init(&S->cancel, 0);
    atomic_init(&S->progress_iter, 0);
    atomic_init(&S->progress_expl, -1.0);
    for (int i = 0; i < UTIL_HASH; i++) S->util_hash[i] = -1;

    /* ranges are read during setup from the caller's arrays */
    S->cfg.range[0] = cfg->range[0];
    S->cfg.range[1] = cfg->range[1];
    int rr = setup_ranges(S);
    if (rr) {
        set_err(err, err_len, rr == -2 ? "a range is empty on this board" : "bad range weights (must be 0..1)");
        ss_destroy(S);
        return NULL;
    }
    if (setup_isomorphism(S)) { set_err(err, err_len, "out of memory"); ss_destroy(S); return NULL; }
    S->cfg.range[0] = S->cfg.range[1] = NULL;

    /* Payoff baseline: each player's value when giving up the starting pot now. */
    {
        double lose[2], win0[2], win1[2];
        S->baseline[0] = S->baseline[1] = 0.0;
        outcome_values(S, 0, 0, 1, win1);
        outcome_values(S, 0, 0, 0, win0);
        lose[0] = win1[0];
        lose[1] = win0[1];
        S->baseline[0] = lose[0];
        S->baseline[1] = lose[1];
        S->pot_value[0] = win0[0] - lose[0];
        S->pot_value[1] = win1[1] - lose[1];
        if (!(S->pot_value[0] > 0) || !(S->pot_value[1] > 0)) {
            set_err(err, err_len, "the pot has no value for a player (check ICM stacks and payouts)");
            ss_destroy(S);
            return NULL;
        }
    }

    /* Tree. */
    BuildState bs;
    memset(&bs, 0, sizeof(bs));
    bs.street_len = S->root_len;
    bs.pot_start = cfg->pot;
    bs.stack = cfg->stack;
    int64_t root = new_nodes(S, 1);
    if (root != 0) { set_err(err, err_len, "out of memory"); ss_destroy(S); return NULL; }
    S->nodes[0].pot = (float)cfg->pot;
    S->nodes[0].street_len = (uint8_t)S->root_len;
    if (cfg->stack <= 0.0) {
        if (make_terminal(S, 0, SS_NODE_SHOWDOWN, 0, 0, 0)) { set_err(err, err_len, "tree build failed"); ss_destroy(S); return NULL; }
    } else if (build_decision(S, 0, &bs)) {
        set_err(err, err_len, "tree build failed (out of memory or too many payoff states)");
        ss_destroy(S);
        return NULL;
    }

    size_t N = 2 * (size_t)(S->n[0] > S->n[1] ? S->n[0] : S->n[1]);   /* x2: best-response walks carry two vectors */
    S->scratch_floats = scratch_need(S, 0, N) + N + 64;
    S->nworkers = S->cfg.threads;

    /* Memory check before the large allocations. */
    size_t sb_bytes = (size_t)S->nboards * (size_t)(S->n[0] + S->n[1]) * sizeof(SortedCombo);
    size_t fixed = S->mem_cur + sb_bytes + (size_t)S->nworkers * S->scratch_floats * sizeof(float);
    size_t need = fixed + S->nstore * sizeof(float);
    size_t need_compact = fixed + S->nstore * sizeof(uint16_t) + 2 * S->ndecisions * sizeof(float) +
                          (size_t)S->nworkers * 8 * (N + 16) * sizeof(float);
    S->compact = cfg->compact_storage == 1 || (cfg->compact_storage == 2 && cfg->memory_limit && need > cfg->memory_limit);
    if (S->compact) {
        /* room to decode two arrays per decision level */
        S->scratch_floats += scratch_need(S, 0, 2 * N) - scratch_need(S, 0, N);
        need = need_compact;
    }
    if (cfg->memory_limit && need > cfg->memory_limit) {
        set_err(err, err_len, "needs %.1f MB, over the %.1f MB limit", need / 1048576.0, cfg->memory_limit / 1048576.0);
        ss_destroy(S);
        return NULL;
    }

    if (S->compact) {
        S->cstore = xcalloc(S, S->nstore, sizeof(uint16_t));
        S->scales = xcalloc(S, 2 * S->ndecisions, sizeof(float));
    } else {
        S->store = xcalloc(S, S->nstore, sizeof(float));
    }
    S->arena = xcalloc(S, (size_t)S->nworkers, sizeof(Arena));
    if ((!S->store && !S->cstore) || (S->compact && !S->scales) || !S->arena) {
        set_err(err, err_len, "out of memory");
        ss_destroy(S);
        return NULL;
    }
    for (int i = 0; i < S->nworkers; i++) {
        S->arena[i].base = xcalloc(S, S->scratch_floats, sizeof(float));
        S->arena[i].cap = S->scratch_floats;
        if (!S->arena[i].base) { set_err(err, err_len, "out of memory"); ss_destroy(S); return NULL; }
    }
    if (setup_boards(S)) { set_err(err, err_len, "out of memory"); ss_destroy(S); return NULL; }

    /* Normaliser: weight of all compatible starting combo pairs. */
    {
        float *m = xcalloc(S, (size_t)S->n[0], sizeof(float));
        if (!m) { set_err(err, err_len, "out of memory"); ss_destroy(S); return NULL; }
        compat_mass(S, 0, S->w[1], m);
        double z = 0.0;
        for (int h = 0; h < S->n[0]; h++) z += (double)S->w[0][h] * m[h];
        xfree(S, m, (size_t)S->n[0], sizeof(float));
        if (!(z > 0)) { set_err(err, err_len, "the ranges have no compatible combos"); ss_destroy(S); return NULL; }
        S->norm = z;
    }
    if (S->nworkers > 1) {
        S->pool = tp_create(S->nworkers);
        if (!S->pool) { set_err(err, err_len, "thread pool failed"); ss_destroy(S); return NULL; }
    }
    return S;
}

void ss_destroy(ss_solver *S)
{
    if (!S) return;
    tp_destroy(S->pool);
    for (int p = 0; p < 2; p++) {
        xfree(S, S->combo[p], (size_t)S->n[p], sizeof(uint16_t));
        xfree(S, S->c1[p], (size_t)S->n[p], 1);
        xfree(S, S->c2[p], (size_t)S->n[p], 1);
        xfree(S, S->w[p], (size_t)S->n[p], sizeof(float));
        xfree(S, S->same[p], (size_t)S->n[p], sizeof(int16_t));
        xfree(S, S->live_of[p], NUM_COMBOS, sizeof(int16_t));
        for (int k = 0; k < 24; k++) xfree(S, S->permc[p][k], (size_t)S->n[p], sizeof(int16_t));
        xfree(S, S->sb_off[p], (size_t)S->nboards, sizeof(int32_t));
        xfree(S, S->sb_cnt[p], (size_t)S->nboards, sizeof(int32_t));
    }
    xfree(S, S->nodes, S->cap_nodes, sizeof(Node));
    if (S->store) xfree(S, S->store, S->nstore, sizeof(float));
    if (S->cstore) xfree(S, S->cstore, S->nstore, sizeof(uint16_t));
    if (S->scales) xfree(S, S->scales, 2 * S->ndecisions, sizeof(float));
    xfree(S, S->cmaps, S->cap_cmaps, 1);
    xfree(S, S->utils, (size_t)S->cap_utils, sizeof(Util));
    xfree(S, S->board_id, 52 * 52, sizeof(int32_t));
    xfree(S, S->sb, S->nsb, sizeof(SortedCombo));
    if (S->arena) {
        for (int i = 0; i < S->nworkers; i++) xfree(S, S->arena[i].base, S->arena[i].cap, sizeof(float));
        xfree(S, S->arena, (size_t)S->nworkers, sizeof(Arena));
    }
    free(S);
}

size_t ss_memory_required(const ss_solver *S) { return S ? S->mem_peak : 0; }

/* ------------------------------------------------------------------------ */
/* Walks                                                                    */

typedef struct {
    ss_solver *S;
    int mode, trav;
    int64_t node;
    const float *reach;
    float *bufs;          /* nslots * stride */
    size_t stride;
    Path path;
} ChanceJob;

static void walk(ss_solver *S, int w, int mode, int64_t idx, int trav, const float *reach, float *out,
                 Path path, int allow_par);

/* Regret matching (current strategy) or normalised strategy sum (average). */
static void strategy_from(const float *src, int k, int n, int positive_part, float *sig, float *tmp)
{
    for (int h = 0; h < n; h++) tmp[h] = 0.0f;
    for (int a = 0; a < k; a++) {
        const float *r = src + (size_t)a * n;
        float *s = sig + (size_t)a * n;
        if (positive_part)
            for (int h = 0; h < n; h++) { float x = r[h] > 0.0f ? r[h] : 0.0f; s[h] = x; tmp[h] += x; }
        else
            for (int h = 0; h < n; h++) { s[h] = r[h]; tmp[h] += r[h]; }
    }
    for (int h = 0; h < n; h++) tmp[h] = tmp[h] > 0.0f ? 1.0f / tmp[h] : 0.0f;
    float uni = 1.0f / (float)k;
    for (int a = 0; a < k; a++) {
        float *s = sig + (size_t)a * n;
        for (int h = 0; h < n; h++) s[h] = tmp[h] > 0.0f ? s[h] * tmp[h] : uni;
    }
}

/*
 * Showdown on a complete board. For each own combo h, with W = reach of opponent
 * combos strictly weaker, Q = equal strength and C = all compatible (each
 * excluding combos that share a card with h):
 *     cfv[h] += uw * W + ut * Q + ul * (C - W - Q)
 * One sweep over own combos in strength order: a cumulative pass over the
 * opponent's sorted list for W, a short pass over each group of equal strength
 * for Q, and per-card sums to remove blocked combos.
 */
static void showdown(const ss_solver *S, int id, int trav, const float *reach, const float *u, float scale, float *out)
{
    int op = 1 - trav;
    const SortedCombo *T = S->sb + S->sb_off[trav][id];
    const SortedCombo *O = S->sb + S->sb_off[op][id];
    int nt = S->sb_cnt[trav][id], no = S->sb_cnt[op][id];
    float uw = u[0] * scale, ul = u[1] * scale, ut = u[2] * scale;
    float dw = uw - ul, dq = ut - ul;
    /* cumulative sums kept in two interleaved halves to shorten dependency chains */
    float cs0[52], cs1[52], cs_eq[52];
    float t0 = 0.0f, t1 = 0.0f;
    memset(cs0, 0, sizeof(cs0));
    memset(cs1, 0, sizeof(cs1));
    memset(cs_eq, 0, sizeof(cs_eq));
    int jl = 0, i = 0;
    while (i < nt) {
        uint16_t st = T[i].str;
        while (jl + 1 < no && O[jl + 1].str < st) {
            float r0 = reach[O[jl].idx], r1 = reach[O[jl + 1].idx];
            t0 += r0; cs0[O[jl].a] += r0; cs0[O[jl].b] += r0;
            t1 += r1; cs1[O[jl + 1].a] += r1; cs1[O[jl + 1].b] += r1;
            jl += 2;
        }
        if (jl < no && O[jl].str < st) {
            float r0 = reach[O[jl].idx];
            t0 += r0; cs0[O[jl].a] += r0; cs0[O[jl].b] += r0;
            jl++;
        }
        int je = jl;
        float tot_eq = 0.0f;
        while (je < no && O[je].str == st) {
            float r = reach[O[je].idx];
            tot_eq += r; cs_eq[O[je].a] += r; cs_eq[O[je].b] += r;
            je++;
        }
        float tot = t0 + t1;
        for (; i < nt && T[i].str == st; i++) {
            int x = T[i].a, y = T[i].b;
            float rs = T[i].same >= 0 ? reach[T[i].same] : 0.0f;   /* the identical combo ties */
            float W = tot - (cs0[x] + cs1[x]) - (cs0[y] + cs1[y]);
            float Q = tot_eq - cs_eq[x] - cs_eq[y] + rs;
            out[T[i].idx] += dw * W + dq * Q;
        }
        for (int j = jl; j < je; j++) { cs_eq[O[j].a] = 0.0f; cs_eq[O[j].b] = 0.0f; }
    }
    /* finish the cumulative sums: they now cover every opponent combo -> ul * C */
    for (; jl < no; jl++) {
        float r = reach[O[jl].idx];
        t0 += r; cs0[O[jl].a] += r; cs0[O[jl].b] += r;
    }
    float tot = t0 + t1;
    for (int c = 0; c < 52; c++) cs0[c] += cs1[c];
    for (i = 0; i < nt; i++) {
        int x = T[i].a, y = T[i].b;
        float rs = T[i].same >= 0 ? reach[T[i].same] : 0.0f;
        out[T[i].idx] += ul * (tot - cs0[x] - cs0[y] + rs);
    }
}

static void terminal(const ss_solver *S, const Node *nd, int trav, const float *reach, float *out, const Path *path)
{
    int nt = S->n[trav];
    const Util *U = &S->utils[nd->util];
    memset(out, 0, sizeof(float) * (size_t)nt);
    if (nd->kind == SS_NODE_FOLD) {
        float u = nd->player == trav ? U->u[trav][1] : U->u[trav][0];
        compat_mass(S, trav, reach, out);
        for (int h = 0; h < nt; h++) out[h] *= u;
        return;
    }
    int known = S->root_len + path->ndealt;
    const float *u = U->u[trav];
    if (known == 5) {
        showdown(S, board_index(S, path->dealt, path->ndealt), trav, reach, u, 1.0f, out);
        return;
    }
    uint8_t on_board[52] = { 0 };
    for (int i = 0; i < S->root_len; i++) on_board[S->root_board[i]] = 1;
    for (int i = 0; i < path->ndealt; i++) on_board[path->dealt[i]] = 1;
    uint8_t d[2] = { 0, 0 };
    if (known == 4) {
        /* one card to come: each compatible pair sees 44 rivers */
        if (path->ndealt == 1) d[0] = path->dealt[0];
        for (int r = 0; r < 52; r++) {
            if (on_board[r]) continue;
            int id;
            if (S->root_len == 4) { d[0] = (uint8_t)r; id = board_index(S, d, 1); }
            else { d[1] = (uint8_t)r; id = board_index(S, d, 2); }
            showdown(S, id, trav, reach, u, 1.0f / 44.0f, out);
        }
        return;
    }
    /* two cards to come from the flop: each compatible pair sees C(45, 2) = 990 runouts */
    for (int a = 0; a < 52; a++) {
        if (on_board[a]) continue;
        for (int b = a + 1; b < 52; b++) {
            if (on_board[b]) continue;
            d[0] = (uint8_t)a; d[1] = (uint8_t)b;
            showdown(S, board_index(S, d, 2), trav, reach, u, 1.0f / 990.0f, out);
        }
    }
}

static void chance_child(ss_solver *S, int w, int mode, const Node *nd, int slot, int trav, const float *reach,
                         float *out, Path path)
{
    int op = 1 - trav;
    Arena *A = &S->arena[w];
    size_t mark = A->top;
    const Node *ch = &S->nodes[nd->child + slot];
    int card = ch->card;
    float *nr = arena_alloc(A, (size_t)S->n[op]);
    const uint8_t *oa = S->c1[op], *ob = S->c2[op];
    for (int v = 0; v < S->n[op]; v++) nr[v] = (oa[v] == card || ob[v] == card) ? 0.0f : reach[v];
    path.dealt[path.ndealt++] = (uint8_t)card;
    walk(S, w, mode, nd->child + slot, trav, nr, out, path, 0);
    A->top = mark;
}

static void chance_task(void *ctx, int slot, int worker)
{
    ChanceJob *J = ctx;
    const Node *nd = &J->S->nodes[J->node];
    chance_child(J->S, worker, J->mode, nd, slot, J->trav, J->reach, J->bufs + (size_t)slot * J->stride, J->path);
}

/* Adds the contribution of every dealt card that maps to this slot. */
static void chance_combine(const ss_solver *S, const Node *nd, int slot, int trav, const float *child,
                           float scale, float *out)
{
    const uint8_t *map = S->cmaps + nd->data;
    int nt = S->n[trav];
    const uint8_t *ta = S->c1[trav], *tb = S->c2[trav];
    for (int c = 0; c < 52; c++) {
        if (map[2 * c] != slot) continue;
        const int16_t *pc = S->permc[trav][map[2 * c + 1]];
        if (map[2 * c + 1] == 0) {
            for (int h = 0; h < nt; h++)
                if (ta[h] != c && tb[h] != c) out[h] += scale * child[h];
        } else {
            for (int h = 0; h < nt; h++)
                if (ta[h] != c && tb[h] != c) out[h] += scale * child[pc[h]];
        }
    }
}

/* Compact storage: regrets as int16 and strategy sums as uint16, each array of a
   node scaled by one float (its largest magnitude maps to the integer range). */
static void decode_block(const ss_solver *S, const Node *nd, int which, size_t count, float *dst)
{
    const uint16_t *src = S->cstore + nd->data + (which ? count : 0);
    float scale = S->scales[2 * (size_t)nd->util + (size_t)which];
    if (which == 0)
        for (size_t i = 0; i < count; i++) dst[i] = (float)(int16_t)src[i] * scale;
    else
        for (size_t i = 0; i < count; i++) dst[i] = (float)src[i] * scale;
}

static void encode_block(ss_solver *S, const Node *nd, int which, size_t count, const float *src)
{
    uint16_t *dst = S->cstore + nd->data + (which ? count : 0);
    float m = 0.0f;
    for (size_t i = 0; i < count; i++) { float a = fabsf(src[i]); m = a > m ? a : m; }
    float full = which == 0 ? 32767.0f : 65535.0f;
    float scale = m > 0.0f ? m / full : 0.0f;
    float inv = m > 0.0f ? full / m : 0.0f;
    S->scales[2 * (size_t)nd->util + (size_t)which] = scale;
    /* round half away from zero; plain arithmetic so the loops vectorise */
    if (which == 0)
        for (size_t i = 0; i < count; i++) {
            float x = src[i] * inv;
            dst[i] = (uint16_t)(int16_t)(int32_t)(x + (x >= 0.0f ? 0.5f : -0.5f));
        }
    else
        for (size_t i = 0; i < count; i++) dst[i] = (uint16_t)(uint32_t)(src[i] * inv + 0.5f);
}

static void walk(ss_solver *S, int w, int mode, int64_t idx, int trav, const float *reach, float *out,
                 Path path, int allow_par)
{
    const Node *nd = &S->nodes[idx];
    int nt = S->n[trav], op = 1 - trav, no = S->n[op];
    Arena *A = &S->arena[w];

    int width = mode == WALK_BREV ? 2 : 1;
    size_t W = (size_t)nt * (size_t)width;

    if (nd->kind == SS_NODE_FOLD || nd->kind == SS_NODE_SHOWDOWN) {
        terminal(S, nd, trav, reach, out, &path);
        if (width == 2) memcpy(out + nt, out, sizeof(float) * (size_t)nt);
        return;
    }

    size_t mark = A->top;
    int k = nd->nchild;

    if (nd->kind == SS_NODE_CHANCE) {
        memset(out, 0, sizeof(float) * W);
        float scale = 1.0f / (float)(52 - (S->root_len + path.ndealt) - 4);
        if (allow_par && S->pool && k > 1) {
            size_t stride = (W + 15) & ~(size_t)15;
            ChanceJob J = { S, mode, trav, idx, reach, arena_alloc(A, stride * (size_t)k), stride, path };
            tp_parallel_for(S->pool, k, chance_task, &J);
            for (int s = 0; s < k; s++)
                for (int ch = 0; ch < width; ch++)
                    chance_combine(S, nd, s, trav, J.bufs + (size_t)s * stride + (size_t)ch * nt, scale, out + (size_t)ch * nt);
        } else {
            float *tmp = arena_alloc(A, W);
            for (int s = 0; s < k; s++) {
                chance_child(S, w, mode, nd, s, trav, reach, tmp, path);
                for (int ch = 0; ch < width; ch++)
                    chance_combine(S, nd, s, trav, tmp + (size_t)ch * nt, scale, out + (size_t)ch * nt);
            }
        }
        A->top = mark;
        return;
    }

    /* decision node */
    int actor = nd->player;
    int na_ = S->n[actor];
    size_t kn = (size_t)k * (size_t)na_;
    float *reg, *ssum;
    int need_reg = mode == WALK_CFR, need_ssum = mode != WALK_CFR || actor != trav;
    if (!S->compact) {
        reg = S->store + nd->data;
        ssum = reg + kn;
    } else {
        reg = need_reg ? arena_alloc(A, kn) : NULL;
        ssum = need_ssum ? arena_alloc(A, kn) : NULL;
        if (need_reg) decode_block(S, nd, 0, kn, reg);
        if (need_ssum) decode_block(S, nd, 1, kn, ssum);
    }

    if (actor == trav) {
        float *cb = arena_alloc(A, (size_t)k * W);
        for (int a = 0; a < k; a++) walk(S, w, mode, nd->child + a, trav, reach, cb + (size_t)a * W, path, allow_par);
        if (mode == WALK_BREV) {
            for (int h = 0; h < nt; h++) out[h] = cb[h];
            for (int a = 1; a < k; a++) {
                const float *c = cb + (size_t)a * W;
                for (int h = 0; h < nt; h++) if (c[h] > out[h]) out[h] = c[h];
            }
            float *sig = arena_alloc(A, (size_t)k * nt);
            float *tmp = arena_alloc(A, (size_t)nt);
            strategy_from(ssum, k, nt, 0, sig, tmp);
            float *ev = out + nt;
            memset(ev, 0, sizeof(float) * (size_t)nt);
            for (int a = 0; a < k; a++) {
                const float *sa = sig + (size_t)a * nt, *c = cb + (size_t)a * W + nt;
                for (int h = 0; h < nt; h++) ev[h] += sa[h] * c[h];
            }
        } else {
            float *sig = arena_alloc(A, (size_t)k * nt);
            float *tmp = arena_alloc(A, (size_t)nt);
            strategy_from(mode == WALK_CFR ? reg : ssum, k, nt, mode == WALK_CFR, sig, tmp);
            memset(out, 0, sizeof(float) * (size_t)nt);
            for (int a = 0; a < k; a++) {
                const float *s = sig + (size_t)a * nt, *c = cb + (size_t)a * nt;
                for (int h = 0; h < nt; h++) out[h] += s[h] * c[h];
            }
            if (mode == WALK_CFR) {
                float dp = S->disc_pos, dn = S->disc_neg;
                for (int a = 0; a < k; a++) {
                    float *r = reg + (size_t)a * nt;
                    const float *c = cb + (size_t)a * nt;
                    for (int h = 0; h < nt; h++) {
                        float old = r[h];
                        r[h] = old * (old > 0.0f ? dp : dn) + (c[h] - out[h]);
                    }
                }
                if (S->compact) encode_block(S, nd, 0, kn, reg);
            }
        }
    } else {
        float *sig = arena_alloc(A, (size_t)k * no);
        float *nr = arena_alloc(A, (size_t)no);
        float *tmp = arena_alloc(A, W);
        strategy_from(mode == WALK_CFR ? reg : ssum, k, no, mode == WALK_CFR, sig, nr);
        memset(out, 0, sizeof(float) * W);
        for (int a = 0; a < k; a++) {
            const float *s = sig + (size_t)a * no;
            float sum = 0.0f;
            for (int v = 0; v < no; v++) { nr[v] = reach[v] * s[v]; sum += nr[v]; }
            if (sum <= 0.0f) continue;
            walk(S, w, mode, nd->child + a, trav, nr, tmp, path, allow_par);
            for (size_t h = 0; h < W; h++) out[h] += tmp[h];
        }
        if (mode == WALK_CFR) {
            float ds = S->disc_strat;
            for (int a = 0; a < k; a++) {
                float *q = ssum + (size_t)a * no;
                const float *s = sig + (size_t)a * no;
                for (int v = 0; v < no; v++) q[v] = q[v] * ds + reach[v] * s[v];
            }
            if (S->compact) encode_block(S, nd, 1, kn, ssum);
        }
    }
    A->top = mark;
}

/* ------------------------------------------------------------------------ */
/* Solving                                                                  */

/* Best-response value and average-strategy value of player p at the root. */
static void root_values(ss_solver *S, int p, double *br, double *ev)
{
    int n = S->n[p];
    float *out = arena_alloc(&S->arena[0], 2 * (size_t)n);
    Path path = { { 0, 0 }, 0 };
    walk(S, 0, WALK_BREV, 0, p, S->w[1 - p], out, path, 1);
    double b = 0.0, e = 0.0;
    for (int h = 0; h < n; h++) {
        b += (double)S->w[p][h] * out[h];
        e += (double)S->w[p][h] * out[n + h];
    }
    S->arena[0].top = 0;
    *br = b / S->norm;
    *ev = e / S->norm;
}

static void compute_exploitability(ss_solver *S)
{
    for (int p = 0; p < 2; p++) {
        double br, ev;
        root_values(S, p, &br, &ev);
        S->last_value[p] = ev;
        S->last_expl[p] = br - ev;
        S->last_expl_pct[p] = 100.0 * (br - ev) / S->pot_value[p];
    }
    S->have_expl = 1;
}

static void cfr_iteration(ss_solver *S)
{
    int t = ++S->iter;
    const ss_config *c = &S->cfg;
    if (t == 1) {
        S->disc_pos = S->disc_neg = S->warm ? 1.0f : 0.0f;
        S->disc_strat = 0.0f;
        S->warm = 0;
    } else {
        double tp = pow((double)(t - 1), c->dcfr_alpha);
        double tn = pow((double)(t - 1), c->dcfr_beta);
        S->disc_pos = (float)(tp / (tp + 1.0));
        S->disc_neg = (float)(tn / (tn + 1.0));
        S->disc_strat = (float)pow((double)(t - 1) / (double)t, c->dcfr_gamma);
    }
    for (int p = 0; p < 2; p++) {
        float *out = arena_alloc(&S->arena[0], (size_t)S->n[p]);
        Path path = { { 0, 0 }, 0 };
        walk(S, 0, WALK_CFR, 0, p, S->w[1 - p], out, path, 1);
        S->arena[0].top = 0;
    }
}

static void fill_result(const ss_solver *S, ss_result *res, int status, double seconds);

int ss_solve(ss_solver *S, ss_result *res)
{
    if (!S) return SS_ERROR;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int status = SS_OK;
    const ss_config *c = &S->cfg;
    int start = S->iter;
    if (S->nodes[0].kind != SS_NODE_DECISION) {
        compute_exploitability(S);
    } else {
        while (S->iter - start < c->max_iterations) {
            if (atomic_load(&S->cancel)) { status = SS_CANCELLED; break; }
            cfr_iteration(S);
            atomic_store(&S->progress_iter, S->iter);
            int last = S->iter - start >= c->max_iterations;
            if (S->iter % c->check_every == 0 || last) {
                compute_exploitability(S);
                double m = S->last_expl_pct[0] > S->last_expl_pct[1] ? S->last_expl_pct[0] : S->last_expl_pct[1];
                atomic_store(&S->progress_expl, m);
                if (c->progress) c->progress(c->progress_user, S->iter, m);
                if (m <= c->target_exploitability_pct) break;
            }
        }
        if (!S->have_expl || status == SS_CANCELLED) compute_exploitability(S);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (res) fill_result(S, res, status, (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec));
    atomic_store(&S->cancel, 0);
    return status;
}

static void fill_result(const ss_solver *S, ss_result *res, int status, double seconds)
{
    memset(res, 0, sizeof(*res));
    res->iterations = S->iter;
    for (int p = 0; p < 2; p++) {
        res->exploitability[p] = S->last_expl[p];
        res->exploitability_pct[p] = S->last_expl_pct[p];
        res->value[p] = S->last_value[p];
        res->pot_value[p] = S->pot_value[p];
    }
    res->seconds = seconds;
    res->peak_memory = S->mem_peak;
    res->tree_nodes = S->nnodes;
    res->compact_storage = S->compact;
    res->status = status;
}

int ss_evaluate(ss_solver *S, ss_result *res)
{
    if (!S) return SS_ERROR;
    compute_exploitability(S);
    if (res) fill_result(S, res, SS_OK, 0.0);
    return SS_OK;
}

int ss_copy_strategy(ss_solver *dst, const ss_solver *src)
{
    if (!dst || !src || dst->nnodes != src->nnodes || dst->nstore != src->nstore || dst->compact != src->compact ||
        dst->n[0] != src->n[0] || dst->n[1] != src->n[1])
        return -1;
    for (size_t i = 0; i < dst->nnodes; i++)
        if (dst->nodes[i].kind != src->nodes[i].kind || dst->nodes[i].nchild != src->nodes[i].nchild) return -1;
    if (dst->compact) {
        memcpy(dst->cstore, src->cstore, sizeof(uint16_t) * dst->nstore);
        memcpy(dst->scales, src->scales, sizeof(float) * 2 * dst->ndecisions);
    } else {
        memcpy(dst->store, src->store, sizeof(float) * dst->nstore);
    }
    dst->iter = src->iter;
    dst->have_expl = 0;
    return 0;
}

int ss_restart_average(ss_solver *S, double regret_scale)
{
    if (!S) return -1;
    float sc = (float)regret_scale;
    for (size_t i = 0; i < S->nnodes; i++) {
        const Node *nd = &S->nodes[i];
        if (nd->kind != SS_NODE_DECISION) continue;
        size_t kn = (size_t)nd->nchild * (size_t)S->n[nd->player];
        if (S->compact) {
            S->scales[2 * (size_t)nd->util] *= sc;
            memset(S->cstore + nd->data + kn, 0, kn * sizeof(uint16_t));
            S->scales[2 * (size_t)nd->util + 1] = 0.0f;
        } else {
            float *r = S->store + nd->data;
            for (size_t j = 0; j < kn; j++) r[j] *= sc;
            memset(r + kn, 0, kn * sizeof(float));
        }
    }
    S->iter = 0;
    S->warm = 1;
    S->have_expl = 0;
    return 0;
}

int ss_warm_start(ss_solver *dst, const ss_solver *src, double regret_scale)
{
    if (ss_copy_strategy(dst, src)) return -1;
    return ss_restart_average(dst, regret_scale);
}

/* State file: magic, then the sizes that must match, then the raw arrays. */
typedef struct {
    char magic[8];
    uint64_t nnodes, nstore, ndecisions;
    int32_t n0, n1, compact, iter;
} StateHeader;

static void state_header(const ss_solver *S, StateHeader *h)
{
    memset(h, 0, sizeof(*h));
    memcpy(h->magic, "ICMSS01", 8);
    h->nnodes = S->nnodes;
    h->nstore = S->nstore;
    h->ndecisions = S->ndecisions;
    h->n0 = S->n[0];
    h->n1 = S->n[1];
    h->compact = S->compact;
    h->iter = S->iter;
}

int ss_save_state(const ss_solver *S, const char *path)
{
    if (!S || !path) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    StateHeader h;
    state_header(S, &h);
    int ok = fwrite(&h, sizeof(h), 1, f) == 1;
    if (ok && S->compact) {
        ok = fwrite(S->cstore, sizeof(uint16_t), S->nstore, f) == S->nstore &&
             fwrite(S->scales, sizeof(float), 2 * S->ndecisions, f) == 2 * S->ndecisions;
    } else if (ok) {
        ok = fwrite(S->store, sizeof(float), S->nstore, f) == S->nstore;
    }
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

int ss_load_state(ss_solver *S, const char *path)
{
    if (!S || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    StateHeader h, want;
    state_header(S, &want);
    int ok = fread(&h, sizeof(h), 1, f) == 1 && !memcmp(h.magic, want.magic, 8) && h.nnodes == want.nnodes &&
             h.nstore == want.nstore && h.ndecisions == want.ndecisions && h.n0 == want.n0 && h.n1 == want.n1 &&
             h.compact == want.compact;
    if (ok && S->compact) {
        ok = fread(S->cstore, sizeof(uint16_t), S->nstore, f) == S->nstore &&
             fread(S->scales, sizeof(float), 2 * S->ndecisions, f) == 2 * S->ndecisions;
    } else if (ok) {
        ok = fread(S->store, sizeof(float), S->nstore, f) == S->nstore;
    }
    fclose(f);
    if (!ok) return -1;
    S->iter = h.iter;
    S->have_expl = 0;
    return 0;
}

void ss_cancel(ss_solver *S) { if (S) atomic_store(&S->cancel, 1); }
int ss_progress_iterations(const ss_solver *S) { return S ? atomic_load(&((ss_solver *)S)->progress_iter) : 0; }
double ss_progress_exploitability(const ss_solver *S) { return S ? atomic_load(&((ss_solver *)S)->progress_expl) : -1.0; }

/* ------------------------------------------------------------------------ */
/* Inspection                                                               */

static int valid_node(const ss_solver *S, int node) { return S && node >= 0 && (size_t)node < S->nnodes; }

int ss_root(const ss_solver *S) { return S ? 0 : -1; }
int ss_node_kind(const ss_solver *S, int node) { return valid_node(S, node) ? S->nodes[node].kind : -1; }
int ss_node_player(const ss_solver *S, int node) { return valid_node(S, node) ? S->nodes[node].player : -1; }
int ss_node_action(const ss_solver *S, int node) { return valid_node(S, node) ? S->nodes[node].act : -1; }
double ss_node_amount(const ss_solver *S, int node) { return valid_node(S, node) ? S->nodes[node].amount : 0.0; }
double ss_node_pot(const ss_solver *S, int node) { return valid_node(S, node) ? S->nodes[node].pot : 0.0; }
int ss_node_card(const ss_solver *S, int node)
{
    return valid_node(S, node) && S->nodes[node].act == SS_ACT_DEAL ? S->nodes[node].card : -1;
}

int ss_node_num_children(const ss_solver *S, int node)
{
    if (!valid_node(S, node)) return -1;
    const Node *nd = &S->nodes[node];
    return nd->kind == SS_NODE_DECISION || nd->kind == SS_NODE_CHANCE ? nd->nchild : 0;
}

int ss_node_child(const ss_solver *S, int node, int i)
{
    if (!valid_node(S, node) || i < 0 || i >= ss_node_num_children(S, node)) return -1;
    return S->nodes[node].child + i;
}

int ss_chance_child(const ss_solver *S, int node, int card, uint8_t perm[4])
{
    if (!valid_node(S, node) || S->nodes[node].kind != SS_NODE_CHANCE || card < 0 || card >= 52) return -1;
    const uint8_t *map = S->cmaps + S->nodes[node].data;
    if (map[2 * card] == 255) return -1;
    if (perm) memcpy(perm, S->perm[map[2 * card + 1]], 4);
    return S->nodes[node].child + map[2 * card];
}

int ss_node_strategy(const ss_solver *S, int node, float *out)
{
    if (!valid_node(S, node) || S->nodes[node].kind != SS_NODE_DECISION || !out) return -1;
    const Node *nd = &S->nodes[node];
    int k = nd->nchild, p = nd->player, n = S->n[p];
    float *buf = NULL;
    const float *ssum;
    if (S->compact) {
        buf = malloc(sizeof(float) * (size_t)k * (size_t)n);
        if (!buf) return -1;
        decode_block(S, nd, 1, (size_t)k * n, buf);
        ssum = buf;
    } else {
        ssum = S->store + nd->data + (size_t)k * n;
    }
    memset(out, 0, sizeof(float) * (size_t)k * NUM_COMBOS);
    for (int h = 0; h < n; h++) {
        double tot = 0.0;
        for (int a = 0; a < k; a++) tot += ssum[(size_t)a * n + h];
        for (int a = 0; a < k; a++)
            out[(size_t)a * NUM_COMBOS + S->combo[p][h]] =
                tot > 0.0 ? (float)(ssum[(size_t)a * n + h] / tot) : 1.0f / (float)k;
    }
    free(buf);
    return k;
}

int ss_player_combos(const ss_solver *S, int player, const uint16_t **combos)
{
    if (!S || player < 0 || player > 1) return -1;
    if (combos) *combos = S->combo[player];
    return S->n[player];
}
