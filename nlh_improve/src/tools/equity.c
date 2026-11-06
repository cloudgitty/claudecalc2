/*
 * Stage 1: exact heads-up preflop all-in equities.
 *
 * usage: equity <out_dir>
 *
 * For every ordered pair of non-overlapping hole-card combos (hero, villain) the
 * equity (win + tie / 2) over all C(48,5) = 1,712,304 boards is computed exactly.
 * Pairs that are the same up to a permutation of suits are computed once.
 *
 * Writes:
 *   eq1326.bin  float32[1326][1326], hero combo (row) vs villain combo (column),
 *               0 where the combos share a card
 *   eq169.bin   float64[169][169] mean equity of hand class h vs class v over the
 *               non-overlapping combo pairs, then int32[169][169] pair counts
 * Hand classes: grid index i * 13 + j with i, j = 0..12 for ranks A..2; i == j pair,
 * i < j suited (ranks i, j), i > j offsuit (ranks j, i). See src/hands.py.
 */
#include "cards.h"
#include "handeval.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int perms[24][4];

static void init_perms(void)
{
    int k = 0;
    for (int a = 0; a < 4; a++)
        for (int b = 0; b < 4; b++)
            for (int c = 0; c < 4; c++)
                for (int d = 0; d < 4; d++) {
                    if (a == b || a == c || a == d || b == c || b == d || c == d) continue;
                    perms[k][0] = a; perms[k][1] = b; perms[k][2] = c; perms[k][3] = d;
                    k++;
                }
}

static int pc(int p, int card) { return make_card(card_rank(card), perms[p][card_suit(card)]); }

int hand_class(int c1, int c2)
{
    int r1 = 12 - card_rank(c1), r2 = 12 - card_rank(c2);   /* 0 = ace */
    if (r1 > r2) { int t = r1; r1 = r2; r2 = t; }          /* r1 = higher rank */
    if (r1 == r2) return r1 * 13 + r1;
    if (card_suit(c1) == card_suit(c2)) return r1 * 13 + r2;
    return r2 * 13 + r1;
}

static double pair_equity(int h1, int h2, int v1, int v2)
{
    uint8_t rest[48];
    int n = 0;
    for (int c = 0; c < 52; c++)
        if (c != h1 && c != h2 && c != v1 && c != v2) rest[n++] = (uint8_t)c;
    uint8_t hc[7], vc[7];
    hc[0] = (uint8_t)h1; hc[1] = (uint8_t)h2;
    vc[0] = (uint8_t)v1; vc[1] = (uint8_t)v2;
    long long win = 0, tie = 0, tot = 0;
    for (int a = 0; a < 48; a++) {
        hc[2] = vc[2] = rest[a];
        for (int b = a + 1; b < 48; b++) {
            hc[3] = vc[3] = rest[b];
            for (int c = b + 1; c < 48; c++) {
                hc[4] = vc[4] = rest[c];
                for (int d = c + 1; d < 48; d++) {
                    hc[5] = vc[5] = rest[d];
                    for (int e = d + 1; e < 48; e++) {
                        hc[6] = vc[6] = rest[e];
                        uint16_t sh = handeval7(hc), sv = handeval7(vc);
                        win += sh > sv;
                        tie += sh == sv;
                        tot++;
                    }
                }
            }
        }
    }
    return (win + 0.5 * tie) / (double)tot;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: equity <out_dir>\n"); return 2; }
    handeval_init();
    init_perms();
    /* canonical representative of every ordered combo pair */
    int32_t *canon = malloc(sizeof(int32_t) * NUM_COMBOS * NUM_COMBOS);
    int32_t *reps = malloc(sizeof(int32_t) * NUM_COMBOS * NUM_COMBOS);
    int nreps = 0;
    for (int i = 0; i < NUM_COMBOS * NUM_COMBOS; i++) canon[i] = -1;
    for (int h = 0; h < NUM_COMBOS; h++) {
        int h1, h2;
        combo_cards(h, &h1, &h2);
        for (int v = 0; v < NUM_COMBOS; v++) {
            int v1, v2;
            combo_cards(v, &v1, &v2);
            if (v1 == h1 || v1 == h2 || v2 == h1 || v2 == h2) continue;
            /* canonical over suit permutations and over swapping hero and villain;
               a swapped pair is stored as -(rep + 2) and has equity 1 - e */
            int best = -1, swapped = 0;
            for (int p = 0; p < 24; p++) {
                int ch = combo_index(pc(p, h1), pc(p, h2)), cv = combo_index(pc(p, v1), pc(p, v2));
                int k1 = ch * NUM_COMBOS + cv, k2 = cv * NUM_COMBOS + ch;
                if (best < 0 || k1 < best) { best = k1; swapped = 0; }
                if (k2 < best) { best = k2; swapped = 1; }
            }
            if (canon[best] < 0) { canon[best] = nreps; reps[nreps++] = best; }
            canon[h * NUM_COMBOS + v] = swapped ? -(canon[best] + 2) : canon[best];
        }
    }
    fprintf(stderr, "%d canonical pairs\n", nreps);
    double *req = malloc(sizeof(double) * nreps);
    int done = 0;
#pragma omp parallel for schedule(dynamic, 16)
    for (int r = 0; r < nreps; r++) {
        int h = reps[r] / NUM_COMBOS, v = reps[r] % NUM_COMBOS, h1, h2, v1, v2;
        combo_cards(h, &h1, &h2);
        combo_cards(v, &v1, &v2);
        req[r] = pair_equity(h1, h2, v1, v2);
#pragma omp atomic
        done++;
        if (done % 2000 == 0) fprintf(stderr, "%d / %d\n", done, nreps);
    }
    float *eq = calloc((size_t)NUM_COMBOS * NUM_COMBOS, sizeof(float));
    static double e169[169][169];
    static int32_t n169[169][169];
    for (int h = 0; h < NUM_COMBOS; h++) {
        int h1, h2;
        combo_cards(h, &h1, &h2);
        for (int v = 0; v < NUM_COMBOS; v++) {
            int k = canon[h * NUM_COMBOS + v];
            if (k == -1) continue;
            double e = k >= 0 ? req[k] : 1.0 - req[-k - 2];
            int v1, v2;
            combo_cards(v, &v1, &v2);
            eq[h * NUM_COMBOS + v] = (float)e;
            int a = hand_class(h1, h2), b = hand_class(v1, v2);
            e169[a][b] += e;
            n169[a][b]++;
        }
    }
    for (int a = 0; a < 169; a++)
        for (int b = 0; b < 169; b++)
            if (n169[a][b]) e169[a][b] /= n169[a][b];
    char path[512];
    snprintf(path, sizeof path, "%s/eq1326.bin", argv[1]);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    fwrite(eq, sizeof(float), (size_t)NUM_COMBOS * NUM_COMBOS, f);
    fclose(f);
    snprintf(path, sizeof path, "%s/eq169.bin", argv[1]);
    f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    fwrite(e169, sizeof(double), 169 * 169, f);
    fwrite(n169, sizeof(int32_t), 169 * 169, f);
    fclose(f);
    fprintf(stderr, "AA vs KK %.4f, AKs vs QQ %.4f, 72o vs AA %.4f\n", e169[0][14], e169[1][28], e169[163][0]);
    return 0;
}
