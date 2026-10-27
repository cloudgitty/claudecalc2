// PLO6 equity engine: class enumeration, Omaha evaluator, Monte Carlo equity
// and a simple postflop realisation simulator. Built as a shared library and
// driven from Python via ctypes (see engine.py).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

// ---------------------------------------------------------------- RNG
typedef struct { uint64_t s[4]; } rng_t;
static inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
static inline uint64_t splitmix(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
static inline void rng_seed(rng_t *r, uint64_t a, uint64_t b) {
    uint64_t x = a * 0x9E3779B97F4A7C15ULL ^ (b + 0x632BE59BD9B4E019ULL);
    for (int i = 0; i < 4; i++) r->s[i] = splitmix(&x);
}
static inline uint64_t rng_next(rng_t *r) {
    uint64_t *s = r->s;
    uint64_t res = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl(s[3], 45);
    return res;
}
static inline uint32_t rng_below(rng_t *r, uint32_t n) {
    return (uint32_t)(((rng_next(r) >> 32) * (uint64_t)n) >> 32);
}
static inline double rng_unif(rng_t *r) { return (rng_next(r) >> 11) * (1.0 / 9007199254740992.0); }

// ---------------------------------------------------------------- evaluator
static uint32_t T23[91 * 455];   // non-flush value by (pair multiset, triple multiset)
static uint32_t FL[8192];        // flush value by 13-bit rank mask (5 bits set)
static int PIDX[13][13];         // a<=b
static int TIDX[13][13][13];     // a<=b<=c
static int PERM[24][4];
static int inited = 0;

static uint32_t eval5_slow(const int *rk, int flush) {
    int cnt[13] = {0}; int mask = 0;
    for (int i = 0; i < 5; i++) { cnt[rk[i]]++; mask |= 1 << rk[i]; }
    int straight = -1;
    for (int top = 12; top >= 4; top--) {
        int m = 0x1F << (top - 4);
        if ((mask & m) == m) { straight = top; break; }
    }
    if (straight < 0 && (mask & 0x100F) == 0x100F) straight = 3;
    // order ranks by (count desc, rank desc)
    int ord[5], n = 0;
    for (int c = 4; c >= 1; c--)
        for (int r = 12; r >= 0; r--)
            if (cnt[r] == c) for (int k = 0; k < c; k++) ord[n++] = r;
    uint32_t tb = 0;
    for (int i = 0; i < 5; i++) tb = (tb << 4) | ord[i];
    int maxc = 0, pairs = 0;
    for (int r = 0; r < 13; r++) { if (cnt[r] > maxc) maxc = cnt[r]; if (cnt[r] == 2) pairs++; }
    int cat;
    if (straight >= 0 && flush) { cat = 8; tb = straight; }
    else if (maxc == 4) cat = 7;
    else if (maxc == 3 && pairs == 1) cat = 6;
    else if (flush) cat = 5;
    else if (straight >= 0) { cat = 4; tb = straight; }
    else if (maxc == 3) cat = 3;
    else if (pairs == 2) cat = 2;
    else if (pairs == 1) cat = 1;
    else cat = 0;
    return ((uint32_t)cat << 20) | tb;
}

void init_engine(void) {
    if (inited) return;
    int n = 0;
    for (int a = 0; a < 13; a++) for (int b = a; b < 13; b++) { PIDX[a][b] = PIDX[b][a] = n++; }
    n = 0;
    int tr[455][3];
    for (int a = 0; a < 13; a++) for (int b = a; b < 13; b++) for (int c = b; c < 13; c++) {
        tr[n][0] = a; tr[n][1] = b; tr[n][2] = c;
        TIDX[a][b][c] = TIDX[a][c][b] = TIDX[b][a][c] = TIDX[b][c][a] = TIDX[c][a][b] = TIDX[c][b][a] = n++;
    }
    for (int a = 0; a < 13; a++) for (int b = a; b < 13; b++)
        for (int t = 0; t < 455; t++) {
            int rk[5] = {a, b, tr[t][0], tr[t][1], tr[t][2]};
            int cnt[13] = {0}, bad = 0;
            for (int i = 0; i < 5; i++) if (++cnt[rk[i]] > 4) bad = 1;
            T23[PIDX[a][b] * 455 + t] = bad ? 0 : eval5_slow(rk, 0);
        }
    for (int m = 0; m < 8192; m++) {
        if (__builtin_popcount(m) != 5) { FL[m] = 0; continue; }
        int rk[5], k = 0;
        for (int r = 0; r < 13; r++) if (m >> r & 1) rk[k++] = r;
        FL[m] = eval5_slow(rk, 1);
    }
    int p = 0, a[4] = {0, 1, 2, 3};
    // all permutations of 0..3 in lexicographic order (same as itertools)
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) for (int l = 0; l < 4; l++) {
        if (i == j || i == k || i == l || j == k || j == l || k == l) continue;
        PERM[p][0] = a[i]; PERM[p][1] = a[j]; PERM[p][2] = a[k]; PERM[p][3] = a[l]; p++;
    }
    inited = 1;
}

uint32_t eval5_cards(const int *c) {   // c: 5 cards 0..51 (for tests)
    int rk[5]; int s0 = c[0] & 3, fl = 1;
    for (int i = 0; i < 5; i++) { rk[i] = c[i] >> 2; if ((c[i] & 3) != s0) fl = 0; }
    return eval5_slow(rk, fl);
}

typedef struct { int off[10]; int suit[10]; int mask[10]; int fsuit; } board_t;
typedef struct { int off[15]; int suit[15]; int mask[15]; } hand_t;

static inline void prep_board(const int *b, board_t *B) {
    int sc[4] = {0};
    for (int i = 0; i < 5; i++) sc[b[i] & 3]++;
    B->fsuit = -1;
    for (int s = 0; s < 4; s++) if (sc[s] >= 3) B->fsuit = s;
    int n = 0;
    for (int i = 0; i < 5; i++) for (int j = i + 1; j < 5; j++) for (int k = j + 1; k < 5; k++) {
        int ri = b[i] >> 2, rj = b[j] >> 2, rk = b[k] >> 2;
        B->off[n] = TIDX[ri][rj][rk];
        int si = b[i] & 3;
        B->suit[n] = (si == (b[j] & 3) && si == (b[k] & 3)) ? si : -1;
        B->mask[n] = (1 << ri) | (1 << rj) | (1 << rk);
        n++;
    }
}
static inline void prep_hand(const int *h, hand_t *H) {
    int n = 0;
    for (int i = 0; i < 6; i++) for (int j = i + 1; j < 6; j++) {
        int ri = h[i] >> 2, rj = h[j] >> 2;
        H->off[n] = PIDX[ri][rj] * 455;
        H->suit[n] = ((h[i] & 3) == (h[j] & 3)) ? (h[i] & 3) : -1;
        H->mask[n] = (1 << ri) | (1 << rj);
        n++;
    }
}
static inline uint32_t omaha_best(const hand_t *H, const board_t *B) {
    uint32_t best = 0;
    for (int p = 0; p < 15; p++) {
        const uint32_t *row = T23 + H->off[p];
        for (int t = 0; t < 10; t++) { uint32_t v = row[B->off[t]]; if (v > best) best = v; }
    }
    if (B->fsuit >= 0) {
        int fs = B->fsuit;
        for (int p = 0; p < 15; p++) if (H->suit[p] == fs)
            for (int t = 0; t < 10; t++) if (B->suit[t] == fs) {
                uint32_t v = FL[H->mask[p] | B->mask[t]]; if (v > best) best = v;
            }
    }
    return best;
}
uint32_t omaha_eval(const int *h6, const int *b5) {
    hand_t H; board_t B; prep_hand(h6, &H); prep_board(b5, &B); return omaha_best(&H, &B);
}

// ---------------------------------------------------------------- classes
static inline int64_t code_of(const int *cards) {
    int r[6], s[6];
    for (int i = 0; i < 6; i++) { r[i] = cards[i] >> 2; s[i] = cards[i] & 3; }
    int bestsu[6], bestr[6], have = 0;
    for (int p = 0; p < 24; p++) {
        int mr[6], ms[6];
        for (int i = 0; i < 6; i++) { mr[i] = r[i]; ms[i] = PERM[p][s[i]]; }
        for (int i = 1; i < 6; i++) {          // sort by (-rank, suit)
            int xr = mr[i], xs = ms[i], j = i - 1;
            while (j >= 0 && (mr[j] < xr || (mr[j] == xr && ms[j] > xs))) { mr[j + 1] = mr[j]; ms[j + 1] = ms[j]; j--; }
            mr[j + 1] = xr; ms[j + 1] = xs;
        }
        int less = !have;
        if (have) for (int i = 0; i < 6; i++) { if (ms[i] != bestsu[i]) { less = ms[i] < bestsu[i]; break; } }
        if (less) { have = 1; for (int i = 0; i < 6; i++) { bestsu[i] = ms[i]; bestr[i] = mr[i]; } }
    }
    int64_t v = 0;
    for (int i = 0; i < 6; i++) v = v * 13 + bestr[i];
    for (int i = 0; i < 6; i++) v = v * 4 + bestsu[i];
    return v;
}
int64_t class_code_cards(const int *cards) { return code_of(cards); }

void class_codes_batch(int64_t n, const int32_t *cards, int64_t *out) {
    #pragma omp parallel for schedule(static)
    for (int64_t i = 0; i < n; i++) out[i] = code_of(cards + 6 * i);
}

// writes codes of all C(52,6) hands in lexicographic order of card tuples
void enumerate_all(int64_t *out) {
    static int64_t start[52];
    int64_t acc = 0;
    // number of hands whose first card is a: C(51-a, 5)
    for (int a = 0; a < 52; a++) {
        start[a] = acc;
        int64_t m = 51 - a, c = 1;
        if (m < 5) c = 0; else for (int k = 0; k < 5; k++) c = c * (m - k) / (k + 1);
        acc += c;
    }
    #pragma omp parallel for schedule(dynamic, 1)
    for (int a = 0; a < 47; a++) {
        int64_t idx = start[a];
        int c[6]; c[0] = a;
        for (c[1] = a + 1; c[1] < 52; c[1]++) for (c[2] = c[1] + 1; c[2] < 52; c[2]++)
        for (c[3] = c[2] + 1; c[3] < 52; c[3]++) for (c[4] = c[3] + 1; c[4] < 52; c[4]++)
        for (c[5] = c[4] + 1; c[5] < 52; c[5]++) out[idx++] = code_of(c);
    }
}

static inline void decode(int64_t code, int *r, int *s) {
    for (int i = 5; i >= 0; i--) { s[i] = code & 3; code >>= 2; }
    for (int i = 5; i >= 0; i--) { r[i] = code % 13; code /= 13; }
}

// random representative of a class (uniform over its hands)
static inline uint64_t rand_rep(int64_t code, rng_t *g, int *out) {
    int r[6], s[6]; decode(code, r, s);
    int p = rng_below(g, 24); uint64_t m = 0;
    for (int i = 0; i < 6; i++) { out[i] = r[i] * 4 + PERM[p][s[i]]; m |= 1ULL << out[i]; }
    return m;
}
void sample_reps(int64_t n, const int64_t *codes, uint64_t seed, uint8_t *out) {
    #pragma omp parallel for schedule(static)
    for (int64_t i = 0; i < n; i++) {
        rng_t g; rng_seed(&g, seed, (uint64_t)i); int h[6];
        rand_rep(codes[i], &g, h);
        for (int k = 0; k < 6; k++) out[6 * i + k] = (uint8_t)h[k];
    }
}

// ---------------------------------------------------------------- equity MC
static inline uint64_t pool_hand(const uint8_t *p, int *v) {
    uint64_t m = 0;
    for (int k = 0; k < 6; k++) { v[k] = p[k]; m |= 1ULL << p[k]; }
    return m;
}
static inline void draw_board(rng_t *g, uint64_t used, int *b) {
    for (int i = 0; i < 5; i++) {
        int c;
        do { c = rng_below(g, 52); } while (used >> c & 1);
        used |= 1ULL << c; b[i] = c;
    }
}

// tasks: hero class code, pool index, number of trials. Outputs per task:
// sum of outcome (1 win, .5 tie), sum of squares, wins+ties made with
// straight-or-better (nut potential), losses holding straight-or-better, and the
// number of trials actually played (a trial is dropped if no villain hand compatible
// with the hero representative is found in 5000 draws, e.g. a tiny range that always
// shares a card with the hero).
void sim_tasks(int64_t ntask, const int64_t *codes, const int32_t *pidx, const int32_t *ntr,
               const uint8_t *pools, const int64_t *poff, const int64_t *plen, uint64_t seed,
               double *o_sum, double *o_sq, double *o_ws, double *o_ls, double *o_cnt) {
    #pragma omp parallel for schedule(dynamic, 16)
    for (int64_t t = 0; t < ntask; t++) {
        rng_t g; rng_seed(&g, seed, (uint64_t)t);
        const uint8_t *pool = pools + 6 * poff[pidx[t]]; int64_t pl = plen[pidx[t]];
        double s = 0, sq = 0, ws = 0, ls = 0, cnt = 0;
        int h[6], v[6], b[6]; hand_t H, V; board_t B;
        for (int i = 0; i < ntr[t]; i++) {
            uint64_t hm = rand_rep(codes[t], &g, h), vm; int tries = 0;
            do { vm = pool_hand(pool + 6 * (int64_t)rng_below(&g, (uint32_t)pl), v); } while ((vm & hm) && ++tries < 5000);
            if (vm & hm) continue;
            cnt += 1;
            draw_board(&g, hm | vm, b);
            prep_board(b, &B); prep_hand(h, &H); prep_hand(v, &V);
            uint32_t eh = omaha_best(&H, &B), ev = omaha_best(&V, &B);
            double o = eh > ev ? 1.0 : (eh == ev ? 0.5 : 0.0);
            s += o; sq += o * o;
            if ((eh >> 20) >= 4) { if (o > 0) ws += o; else ls += 1; }
        }
        o_sum[t] = s; o_sq[t] = sq; o_ws[t] = ws; o_ls[t] = ls; o_cnt[t] = cnt;
    }
}

// ---------------------------------------------------------------- postflop realisation
// Equity of hand h vs a pool given known board cards (nb of them), by MC.
static double eq_known(rng_t *g, const int *h, const int *board, int nb,
                       const uint8_t *pool, int64_t pl, int m) {
    uint64_t hm = 0; for (int i = 0; i < 6; i++) hm |= 1ULL << h[i];
    uint64_t bm = 0; for (int i = 0; i < nb; i++) bm |= 1ULL << board[i];
    hand_t H, V; board_t B; prep_hand(h, &H);
    int v[6], b[5]; double s = 0;
    for (int i = 0; i < m; i++) {
        uint64_t vm; int tries = 0;
        do { vm = pool_hand(pool + 6 * (int64_t)rng_below(g, (uint32_t)pl), v); tries++; } while ((vm & (hm | bm)) && tries < 10000);
        for (int k = 0; k < nb; k++) b[k] = board[k];
        uint64_t used = hm | bm | vm;
        for (int k = nb; k < 5; k++) { int c; do { c = rng_below(g, 52); } while (used >> c & 1); used |= 1ULL << c; b[k] = c; }
        prep_board(b, &B); prep_hand(v, &V);
        uint32_t eh = omaha_best(&H, &B), ev = omaha_best(&V, &B);
        s += eh > ev ? 1.0 : (eh == ev ? 0.5 : 0.0);
    }
    return s / m;
}

// One street of betting with a fixed equity-bucket strategy.
// player 0 = OOP, 1 = IP. inv[] = chips put in this hand from flop on.
// Returns 1 if someone folded (winner in *winner).
static int street(double *pot, double *stk, double e[2], double bet_th, double call_margin,
                  double raise_th, int *winner, double *inv) {
    double tocall = 0; int aggr = -1, raises = 0;
    // OOP acts
    int actor = 0, checks = 0;
    for (int step = 0; step < 8; step++) {
        double ee = e[actor];
        if (aggr < 0) {
            if (ee >= bet_th && stk[actor] > 0) {
                double b = *pot; if (b > stk[actor]) b = stk[actor];
                stk[actor] -= b; inv[actor] += b; *pot += b; tocall = b; aggr = actor;
            } else { checks++; if (checks == 2) return 0; }
        } else {
            double c = tocall; if (c > stk[actor]) c = stk[actor];
            double req = c / (*pot + c);
            if (ee >= raise_th && raises < 2 && stk[actor] > c && stk[1 - actor] > 0) {
                // pot raise: call then raise the pot
                double r = c + (*pot + c); if (r > stk[actor]) r = stk[actor];
                double extra = r - c;
                if (extra > stk[1 - actor]) { extra = stk[1 - actor]; r = c + extra; }
                stk[actor] -= r; inv[actor] += r; *pot += r; tocall = extra; aggr = actor; raises++;
            } else if (ee >= req + call_margin) {
                stk[actor] -= c; inv[actor] += c; *pot += c; return 0;
            } else { *winner = aggr; return 1; }   // actor folds
        }
        actor = 1 - actor;
    }
    return 0;
}

// Simulate n hand pairs (OOP from pool A, IP from pool B) from flop to river.
// Outputs per sample: OOP all-in equity proxy is not needed; returns the realised
// share of the starting pot for OOP and IP and the actual showdown/all-in result
// share, so R = sum(share)/sum(eq) can be computed in Python. eq is estimated with
// mq samples (preflop equity of the actual hand pair vs each other's range is not
// what R uses: R uses equity vs range, returned in o_eq).
void sim_postflop(int64_t n, const uint8_t *poolA, int64_t lenA, const uint8_t *poolB, int64_t lenB,
                  double pot0, double stack0, int mq, double bet_th, double call_margin, double raise_th,
                  uint64_t seed, double *o_share_oop, double *o_eq_oop, double *o_share_ip, double *o_eq_ip) {
    #pragma omp parallel for schedule(dynamic, 8)
    for (int64_t t = 0; t < n; t++) {
        rng_t g; rng_seed(&g, seed, (uint64_t)t);
        int a[6], b[6], bd[5];
        uint64_t am, bm;
        am = pool_hand(poolA + 6 * (int64_t)rng_below(&g, (uint32_t)lenA), a);
        do { bm = pool_hand(poolB + 6 * (int64_t)rng_below(&g, (uint32_t)lenB), b); } while (bm & am);
        draw_board(&g, am | bm, bd);
        // all-in equity of each hand vs the opponent's range (preflop, no board)
        double eqa = eq_known(&g, a, bd, 0, poolB, lenB, mq * 4);
        double eqb = eq_known(&g, b, bd, 0, poolA, lenA, mq * 4);
        double pot = pot0, stk[2] = {stack0, stack0}, inv[2] = {0, 0};
        int winner = -1, folded = 0;
        int nb_st[3] = {3, 4, 5};
        for (int st = 0; st < 3 && !folded; st++) {
            if (stk[0] <= 0 || stk[1] <= 0) break;
            double e[2];
            e[0] = eq_known(&g, a, bd, nb_st[st], poolB, lenB, mq);
            e[1] = eq_known(&g, b, bd, nb_st[st], poolA, lenA, mq);
            folded = street(&pot, stk, e, bet_th, call_margin, raise_th, &winner, inv);
        }
        double share0;
        if (folded) share0 = (winner == 0) ? pot : 0.0;
        else {
            hand_t HA, HB; board_t B; prep_board(bd, &B); prep_hand(a, &HA); prep_hand(b, &HB);
            uint32_t ea = omaha_best(&HA, &B), eb = omaha_best(&HB, &B);
            share0 = ea > eb ? pot : (ea == eb ? pot / 2 : 0.0);
        }
        // net result relative to the flop pot: pot won minus own postflop investment
        o_share_oop[t] = (share0 - inv[0]) / pot0;
        o_share_ip[t] = ((pot - share0) - inv[1]) / pot0;
        o_eq_oop[t] = eqa; o_eq_ip[t] = eqb;
    }
}

// Percentile of hand h's equity within its own range on the known board:
// K hands sampled from the own pool (avoiding board cards), each rated by MC.
static double pctl_known(rng_t *g, const int *h, const int *board, int nb, const uint8_t *own, int64_t lo,
                         const uint8_t *opp, int64_t lp, double eh, int K, int m2) {
    uint64_t bm = 0; for (int i = 0; i < nb; i++) bm |= 1ULL << board[i];
    double below = 0; int s[6];
    for (int k = 0; k < K; k++) {
        uint64_t sm; int tries = 0;
        do { sm = pool_hand(own + 6 * (int64_t)rng_below(g, (uint32_t)lo), s); tries++; } while ((sm & bm) && tries < 10000);
        double es = eq_known(g, s, board, nb, opp, lp, m2);
        below += es < eh ? 1.0 : (es == eh ? 0.5 : 0.0);
    }
    return below / K;
}

// Fixed percentile strategy (identical for both seats). p = own-range percentile.
// Unopened: bet pot with p >= VAL or p <= BLF. Facing a bet: raise pot with p >= RAI,
// call with p >= 1 - MDF (MDF = pot / (pot + bet)), facing a raise call with p >= RCALL.
static int street_p(rng_t *g, double *pot, double *stk, const double p[2], int *winner, double *inv,
                    double VAL, double BLF, double RAI, double RCALL) {
    double tocall = 0; int aggr = -1, raises = 0, checks = 0, actor = 0;
    double lo[2] = {0, 0}, hi[2] = {1, 1};      // own percentile interval this street
    for (int step = 0; step < 8; step++) {
        double pp = p[actor];
        if (aggr < 0) {
            // first to act traps half of its strongest hands (keeps the checking range uncapped)
            int trap = (checks == 0 && pp >= RAI && rng_unif(g) < 0.5);
            if ((pp >= VAL || pp <= BLF) && stk[actor] > 0 && !trap) {
                double b = *pot; if (b > stk[actor]) b = stk[actor];
                stk[actor] -= b; inv[actor] += b; *pot += b; tocall = b; aggr = actor;
            } else {
                lo[actor] = BLF; hi[actor] = (checks == 0) ? 1.0 : VAL;
                if (checks == 0) { /* checking range = (BLF,VAL) plus half of [RAI,1] */
                    double mass_mid = VAL - BLF, mass_top = 0.5 * (1 - RAI);
                    hi[actor] = 1.0; lo[actor] = 1.0 - (mass_mid + mass_top);
                    if (lo[actor] < BLF) lo[actor] = BLF;
                } else hi[actor] = VAL;
                if (++checks == 2) return 0;
            }
        } else {
            double c = tocall; if (c > stk[actor]) c = stk[actor];
            double potb = *pot - tocall;               // pot before the bet
            double mdf = potb / (potb + tocall);       // defend MDF of the current (capped) range
            double thr = raises == 0 ? hi[actor] - mdf * (hi[actor] - lo[actor]) : RCALL;
            double rth = raises == 0 ? RAI : 0.5 * (1 + RAI);
            if (pp >= rth && raises < 2 && stk[actor] > c && stk[1 - actor] > 0) {
                double r = c + (*pot + c); if (r > stk[actor]) r = stk[actor];
                double extra = r - c;
                if (extra > stk[1 - actor]) { extra = stk[1 - actor]; r = c + extra; }
                stk[actor] -= r; inv[actor] += r; *pot += r; tocall = extra; aggr = actor; raises++;
            } else if (pp >= thr) {
                stk[actor] -= c; inv[actor] += c; *pot += c; return 0;
            } else { *winner = aggr; return 1; }
        }
        actor = 1 - actor;
    }
    return 0;
}

void sim_postflop_p(int64_t n, const uint8_t *poolA, int64_t lenA, const uint8_t *poolB, int64_t lenB,
                    double pot0, double stack0, int mq, int K, int m2,
                    double VAL, double BLF, double RAI, double RCALL, uint64_t seed,
                    double *o_share_oop, double *o_eq_oop, double *o_share_ip, double *o_eq_ip) {
    #pragma omp parallel for schedule(dynamic, 4)
    for (int64_t t = 0; t < n; t++) {
        rng_t g; rng_seed(&g, seed, (uint64_t)t);
        int a[6], b[6], bd[5];
        uint64_t am = pool_hand(poolA + 6 * (int64_t)rng_below(&g, (uint32_t)lenA), a), bm;
        do { bm = pool_hand(poolB + 6 * (int64_t)rng_below(&g, (uint32_t)lenB), b); } while (bm & am);
        draw_board(&g, am | bm, bd);
        double eqa = eq_known(&g, a, bd, 0, poolB, lenB, mq * 4);
        double eqb = eq_known(&g, b, bd, 0, poolA, lenA, mq * 4);
        double pot = pot0, stk[2] = {stack0, stack0}, inv[2] = {0, 0};
        int winner = -1, folded = 0, nbs[3] = {3, 4, 5};
        for (int st = 0; st < 3 && !folded; st++) {
            if (stk[0] <= 0 || stk[1] <= 0) break;
            double p[2];
            double ea = eq_known(&g, a, bd, nbs[st], poolB, lenB, mq);
            double eb = eq_known(&g, b, bd, nbs[st], poolA, lenA, mq);
            p[0] = pctl_known(&g, a, bd, nbs[st], poolA, lenA, poolB, lenB, ea, K, m2);
            p[1] = pctl_known(&g, b, bd, nbs[st], poolB, lenB, poolA, lenA, eb, K, m2);
            folded = street_p(&g, &pot, stk, p, &winner, inv, VAL, BLF, RAI, RCALL);
        }
        double share0;
        if (folded) share0 = (winner == 0) ? pot : 0.0;
        else {
            hand_t HA, HB; board_t B; prep_board(bd, &B); prep_hand(a, &HA); prep_hand(b, &HB);
            uint32_t ea = omaha_best(&HA, &B), eb = omaha_best(&HB, &B);
            share0 = ea > eb ? pot : (ea == eb ? pot / 2 : 0.0);
        }
        o_share_oop[t] = (share0 - inv[0]) / pot0;
        o_share_ip[t] = ((pot - share0) - inv[1]) / pot0;
        o_eq_oop[t] = eqa; o_eq_ip[t] = eqb;
    }
}

// Card-removal-aware mean of per-hand values (e.g. villain action frequencies)
// over a villain pool, for each hero class: villain hands colliding with a random
// representative of the hero class are rejected.
void sample_values(int64_t n, const int64_t *codes, const uint8_t *pool, int64_t pl,
                   const float *vals, int nv, int m, uint64_t seed, float *out) {
    #pragma omp parallel for schedule(dynamic, 256)
    for (int64_t t = 0; t < n; t++) {
        rng_t g; rng_seed(&g, seed, (uint64_t)t);
        double acc[8] = {0}; int h[6], v[6], ok = 0;
        for (int i = 0; i < m; i++) {
            uint64_t hm = rand_rep(codes[t], &g, h), vm; int64_t j; int tries = 0;
            do { j = rng_below(&g, (uint32_t)pl); vm = pool_hand(pool + 6 * j, v); } while ((vm & hm) && ++tries < 2000);
            if (vm & hm) continue;           // no compatible villain hand found: skip
            ok++;
            for (int k = 0; k < nv; k++) acc[k] += vals[j * nv + k];
        }
        for (int k = 0; k < nv; k++) out[t * nv + k] = ok ? (float)(acc[k] / ok) : -1.0f;
    }
}

// Exact all-in equity of fixed hand h vs fixed hand v over all C(40,5) boards,
// and the Monte Carlo estimate with n random boards (to validate the sampler).
double exact_hvh(const int *h, const int *v) {
    uint64_t used = 0; for (int i = 0; i < 6; i++) used |= (1ULL << h[i]) | (1ULL << v[i]);
    int rest[40], k = 0; for (int c = 0; c < 52; c++) if (!(used >> c & 1)) rest[k++] = c;
    hand_t H, V; prep_hand(h, &H); prep_hand(v, &V);
    double s = 0; long cnt = 0;
    #pragma omp parallel for reduction(+:s, cnt) schedule(dynamic)
    for (int a = 0; a < 36; a++) {
        board_t B; int b[5]; b[0] = rest[a];
        for (int i = a + 1; i < 40; i++) for (int j = i + 1; j < 40; j++) for (int l = j + 1; l < 40; l++) for (int m = l + 1; m < 40; m++) {
            b[1] = rest[i]; b[2] = rest[j]; b[3] = rest[l]; b[4] = rest[m];
            prep_board(b, &B);
            uint32_t eh = omaha_best(&H, &B), ev = omaha_best(&V, &B);
            s += eh > ev ? 1.0 : (eh == ev ? 0.5 : 0.0); cnt++;
        }
    }
    return s / cnt;
}

double mc_hvh(const int *h, const int *v, int n, uint64_t seed) {
    uint64_t used = 0; for (int i = 0; i < 6; i++) used |= (1ULL << h[i]) | (1ULL << v[i]);
    hand_t H, V; prep_hand(h, &H); prep_hand(v, &V);
    double s = 0;
    #pragma omp parallel for reduction(+:s)
    for (int t = 0; t < n; t++) {
        rng_t g; rng_seed(&g, seed, (uint64_t)t); int b[5]; board_t B;
        draw_board(&g, used, b); prep_board(b, &B);
        uint32_t eh = omaha_best(&H, &B), ev = omaha_best(&V, &B);
        s += eh > ev ? 1.0 : (eh == ev ? 0.5 : 0.0);
    }
    return s / n;
}

int engine_threads(void) { return omp_get_max_threads(); }
