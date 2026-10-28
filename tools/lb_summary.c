/* Library builder: flop summaries and raw flop strategies of a finished solve. */
#include "lb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int child_with_action(const ss_solver *s, int node, int act)
{
    int k = ss_node_num_children(s, node);
    for (int i = 0; i < k; i++) {
        int c = ss_node_child(s, node, i);
        if (ss_node_action(s, c) == act) return c;
    }
    return -1;
}

static int is_bet(int act) { return act == SS_ACT_BET || act == SS_ACT_ALLIN; }
static int is_raise(int act) { return act == SS_ACT_RAISE || act == SS_ACT_ALLIN; }

/* Size code of a bet into a pot of size pot. */
static int size_code(const ss_solver *s, int child, double pot)
{
    if (ss_node_action(s, child) == SS_ACT_ALLIN) return LR_SIZE_ALLIN;
    double r = ss_node_amount(s, child) / pot;
    return fabs(r - 0.33) <= fabs(r - 0.75) ? LR_SIZE_33 : LR_SIZE_75;
}

typedef struct {
    const ss_solver *s;
    int hero;
    const int8_t *cat;
    double num[HC_NUM_CATEGORIES], den[HC_NUM_CATEGORIES];
} AllinWalk;

/* Walks the flop decision nodes with hero reach r and opponent reach o; at every
   node where hero faces an opponent all-in, pools the call frequency by category,
   weighted by the probability of reaching the node with each hand. */
static void allin_walk(AllinWalk *W, int node, const float *r, const float *o)
{
    const ss_solver *s = W->s;
    if (ss_node_kind(s, node) != SS_NODE_DECISION) return;
    int k = ss_node_num_children(s, node);
    int actor = ss_node_player(s, node);
    float *st = malloc(sizeof(float) * (size_t)k * NUM_COMBOS);
    float *nr = malloc(sizeof(float) * NUM_COMBOS);
    if (!st || !nr) { free(st); free(nr); return; }
    ss_node_strategy(s, node, st);

    if (actor == W->hero && ss_node_action(s, node) == SS_ACT_ALLIN) {
        int call = -1;
        for (int i = 0; i < k; i++) if (ss_node_action(s, ss_node_child(s, node, i)) == SS_ACT_CALL) call = i;
        double m = 0.0;
        for (int i = 0; i < NUM_COMBOS; i++) m += o[i];
        if (call >= 0 && m > 0.0)
            for (int i = 0; i < NUM_COMBOS; i++) {
                if (r[i] <= 0.0f || W->cat[i] < 0) continue;
                W->num[W->cat[i]] += m * r[i] * st[call * NUM_COMBOS + i];
                W->den[W->cat[i]] += m * r[i];
            }
    }
    for (int a = 0; a < k; a++) {
        int c = ss_node_child(s, node, a);
        const float *sa = st + (size_t)a * NUM_COMBOS;
        if (actor == W->hero) {
            for (int i = 0; i < NUM_COMBOS; i++) nr[i] = r[i] * sa[i];
            allin_walk(W, c, nr, o);
        } else {
            for (int i = 0; i < NUM_COMBOS; i++) nr[i] = o[i] * sa[i];
            allin_walk(W, c, r, nr);
        }
    }
    free(st);
    free(nr);
}

void lb_summarise(const ss_solver *s, const uint8_t flop[3], const float *ranges, int hero, LbSummary *out)
{
    memset(out, 0, sizeof(*out));
    out->check_raise_pct = NAN;
    out->bet_size = -1;
    out->stackoff_category = -1;
    const float *wh = ranges + (size_t)hero * NUM_COMBOS;
    const float *wo = ranges + (size_t)(1 - hero) * NUM_COMBOS;

    int8_t cat[NUM_COMBOS];
    float h0[NUM_COMBOS], o0[NUM_COMBOS];
    for (int i = 0; i < NUM_COMBOS; i++) {
        int a, b;
        combo_cards(i, &a, &b);
        uint8_t hole[2] = { (uint8_t)a, (uint8_t)b };
        int blocked = a == flop[0] || a == flop[1] || a == flop[2] || b == flop[0] || b == flop[1] || b == flop[2];
        cat[i] = blocked ? -1 : (int8_t)hand_category(hole, flop);
        h0[i] = blocked ? 0.0f : wh[i];
        o0[i] = blocked ? 0.0f : wo[i];
    }

    float *st = malloc(sizeof(float) * 16 * NUM_COMBOS);
    if (!st) return;
    int root = ss_root(s);
    int node = hero == 0 ? root : child_with_action(s, root, SS_ACT_CHECK);

    /* 1, 2, 5: first chance to bet */
    if (node >= 0 && ss_node_kind(s, node) == SS_NODE_DECISION) {
        int k = ss_node_strategy(s, node, st);
        double pot = ss_node_pot(s, node);
        double tot = 0, bet = 0, bluff = 0, size_mass[3] = { 0, 0, 0 };
        double cm[HC_NUM_CATEGORIES] = { 0 }, cb[HC_NUM_CATEGORIES] = { 0 };
        for (int a = 0; a < k; a++) {
            int c = ss_node_child(s, node, a);
            if (!is_bet(ss_node_action(s, c))) continue;
            int code = size_code(s, c, pot);
            for (int i = 0; i < NUM_COMBOS; i++) size_mass[code] += h0[i] * st[a * NUM_COMBOS + i];
        }
        for (int i = 0; i < NUM_COMBOS; i++) {
            if (h0[i] <= 0.0f) continue;
            double b = 0;
            for (int a = 0; a < k; a++)
                if (is_bet(ss_node_action(s, ss_node_child(s, node, a)))) b += st[a * NUM_COMBOS + i];
            tot += h0[i];
            bet += h0[i] * b;
            cm[cat[i]] += h0[i];
            cb[cat[i]] += h0[i] * b;
            if (cat[i] == HC_WEAK_DRAW || cat[i] == HC_AIR) bluff += h0[i] * b;
        }
        out->bet_pct = tot > 0 ? 100.0 * bet / tot : 0.0;
        out->bluff_pct = bet > 0 ? 100.0 * bluff / bet : 0.0;
        if (bet > 0) {
            int best = 0;
            for (int z = 1; z < 3; z++) if (size_mass[z] > size_mass[best]) best = z;
            out->bet_size = best;
        }
        for (int c = 0; c < HC_NUM_CATEGORIES; c++) {
            out->category_mass[c] = tot > 0 ? 100.0 * cm[c] / tot : 0.0;
            out->category_bet_pct[c] = cm[c] > 0 ? 100.0 * cb[c] / cm[c] : NAN;
        }
    }

    /* 3: check-raise, out of position only */
    if (hero == 0) {
        int k = ss_node_strategy(s, root, st);
        int chk = -1;
        for (int a = 0; a < k; a++) if (ss_node_action(s, ss_node_child(s, root, a)) == SS_ACT_CHECK) chk = a;
        int ipn = chk >= 0 ? ss_node_child(s, root, chk) : -1;
        if (ipn >= 0 && ss_node_kind(s, ipn) == SS_NODE_DECISION) {
            float r[NUM_COMBOS];
            for (int i = 0; i < NUM_COMBOS; i++) r[i] = h0[i] * st[chk * NUM_COMBOS + i];
            float *sip = malloc(sizeof(float) * 16 * NUM_COMBOS);
            if (sip) {
                int kip = ss_node_strategy(s, ipn, sip);
                double wsum = 0, acc = 0, plain = 0;
                int nodes = 0;
                for (int a = 0; a < kip; a++) {
                    int fb = ss_node_child(s, ipn, a);
                    if (!is_bet(ss_node_action(s, fb)) || ss_node_kind(s, fb) != SS_NODE_DECISION) continue;
                    int kf = ss_node_strategy(s, fb, st);
                    int any_raise = 0;
                    double rr = 0, rt = 0, m = 0;
                    for (int b = 0; b < kf; b++) {
                        if (!is_raise(ss_node_action(s, ss_node_child(s, fb, b)))) continue;
                        any_raise = 1;
                        for (int i = 0; i < NUM_COMBOS; i++) rr += r[i] * st[b * NUM_COMBOS + i];
                    }
                    if (!any_raise) continue;
                    for (int i = 0; i < NUM_COMBOS; i++) { rt += r[i]; m += o0[i] * sip[a * NUM_COMBOS + i]; }
                    double f = rt > 0 ? rr / rt : 0.0;
                    acc += m * f;
                    wsum += m;
                    plain += f;
                    nodes++;
                }
                if (nodes) out->check_raise_pct = 100.0 * (wsum > 0 ? acc / wsum : plain / nodes);
                free(sip);
            }
        }
    }
    free(st);

    /* 4: stack-off threshold */
    AllinWalk W;
    memset(&W, 0, sizeof W);
    W.s = s;
    W.hero = hero;
    W.cat = cat;
    if (hero == 0) allin_walk(&W, root, h0, o0);
    else allin_walk(&W, root, h0, o0);   /* same walk: actor decides whose reach changes */
    for (int c = 0; c < HC_NUM_CATEGORIES; c++)
        out->category_call_allin_pct[c] = W.den[c] > 0 ? 100.0 * W.num[c] / W.den[c] : NAN;
    for (int c = HC_NUM_CATEGORIES - 1; c >= 0; c--)
        if (W.den[c] > 0 && W.num[c] >= 0.5 * W.den[c]) { out->stackoff_category = c; break; }
}

/* ------------------------------------------------------------------------ */
/* Raw strategies: every flop decision node, 8-bit quantised, zlib-compressed. */

typedef struct { uint8_t *buf; size_t len, cap; int ok; } Buf;

static void put(Buf *b, const void *p, size_t n)
{
    if (!b->ok) return;
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 65536;
        while (nc < b->len + n) nc *= 2;
        uint8_t *q = realloc(b->buf, nc);
        if (!q) { b->ok = 0; return; }
        b->buf = q;
        b->cap = nc;
    }
    memcpy(b->buf + b->len, p, n);
    b->len += n;
}
static void put_u8(Buf *b, unsigned v) { uint8_t x = (uint8_t)v; put(b, &x, 1); }
static void put_u16(Buf *b, unsigned v) { uint8_t x[2] = { (uint8_t)(v & 255), (uint8_t)(v >> 8) }; put(b, x, 2); }

static void raw_nodes(const ss_solver *s, int node, int parent, int slot, Buf *b, int *count, float *st)
{
    if (ss_node_kind(s, node) != SS_NODE_DECISION) return;
    int me = (*count)++;
    int k = ss_node_strategy(s, node, st);
    int p = ss_node_player(s, node);
    const uint16_t *combos;
    int n = ss_player_combos(s, p, &combos);
    put_u16(b, (unsigned)(parent < 0 ? 0xffff : parent));
    put_u8(b, (unsigned)slot);
    put_u8(b, (unsigned)p);
    put_u8(b, (unsigned)k);
    for (int a = 0; a < k; a++) {
        int c = ss_node_child(s, node, a);
        put_u8(b, (unsigned)ss_node_action(s, c));
        put_u16(b, f16_from_float((float)ss_node_amount(s, c)));
    }
    for (int a = 0; a < k; a++)
        for (int i = 0; i < n; i++) {
            float x = st[(size_t)a * NUM_COMBOS + combos[i]];
            put_u8(b, (unsigned)(x * 255.0f + 0.5f));
        }
    float *mine = malloc(sizeof(float) * 16 * NUM_COMBOS);
    if (!mine) { b->ok = 0; return; }
    for (int a = 0; a < k; a++) raw_nodes(s, ss_node_child(s, node, a), me, a, b, count, mine);
    free(mine);
}

int lb_write_raw(const ss_solver *s, const char *path, size_t *bytes)
{
    Buf b = { NULL, 0, 0, 1 };
    put(&b, "ICMRAW1", 8);
    for (int p = 0; p < 2; p++) {
        const uint16_t *combos;
        int n = ss_player_combos(s, p, &combos);
        put_u16(&b, (unsigned)n);
        for (int i = 0; i < n; i++) put_u16(&b, combos[i]);
    }
    size_t count_pos = b.len;
    put_u16(&b, 0);
    int count = 0;
    float *st = malloc(sizeof(float) * 16 * NUM_COMBOS);
    if (!st) { free(b.buf); return -1; }
    raw_nodes(s, ss_root(s), -1, 0, &b, &count, st);
    free(st);
    if (!b.ok) { free(b.buf); return -1; }
    b.buf[count_pos] = (uint8_t)(count & 255);
    b.buf[count_pos + 1] = (uint8_t)(count >> 8);

    uLongf zlen = compressBound(b.len);
    uint8_t *z = malloc(zlen + 8);
    if (!z) { free(b.buf); return -1; }
    /* 4-byte little-endian uncompressed length, then the zlib stream */
    z[0] = (uint8_t)(b.len & 255); z[1] = (uint8_t)(b.len >> 8); z[2] = (uint8_t)(b.len >> 16); z[3] = (uint8_t)(b.len >> 24);
    int r = compress2(z + 4, &zlen, b.buf, b.len, 9);
    free(b.buf);
    if (r != Z_OK) { free(z); return -1; }
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    int ok = f && fwrite(z, 1, zlen + 4, f) == zlen + 4;
    if (f && fclose(f) != 0) ok = 0;
    free(z);
    if (!ok || rename(tmp, path) != 0) { remove(tmp); return -1; }
    if (bytes) *bytes = zlen + 4;
    return 0;
}
