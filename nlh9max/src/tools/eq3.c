/* Sample of exact-ish 3-way all-in equities for fitting the 3-way payoff model.
   usage: eq3 <triples> <boards> <seed>   prints: c1 c2 c3 p1 p2 p3 (combo indices, pot shares) */
#include "cards.h"
#include "handeval.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned long long st;
static unsigned rnd(void) { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return (unsigned)(st >> 11); }
int main(int argc, char **argv)
{
    int nt = atoi(argv[1]), nb = atoi(argv[2]);
    st = 88172645463325252ULL ^ (unsigned long long)atoll(argv[3]);
    handeval_init();
    for (int t = 0; t < nt; t++) {
        int c[6], used[52] = { 0 };
        for (int i = 0; i < 6; i++) { int x; do x = rnd() % 52; while (used[x]); used[x] = 1; c[i] = x; }
        double share[3] = { 0, 0, 0 };
        for (int b = 0; b < nb; b++) {
            int bd[5], u2[52];
            for (int i = 0; i < 52; i++) u2[i] = used[i];
            for (int i = 0; i < 5; i++) { int x; do x = rnd() % 52; while (u2[x]); u2[x] = 1; bd[i] = x; }
            uint16_t s[3];
            for (int p = 0; p < 3; p++) {
                uint8_t cards[7] = { (uint8_t)c[2 * p], (uint8_t)c[2 * p + 1] };
                for (int i = 0; i < 5; i++) cards[2 + i] = (uint8_t)bd[i];
                s[p] = handeval7(cards);
            }
            uint16_t m = s[0] > s[1] ? s[0] : s[1]; if (s[2] > m) m = s[2];
            int nw = (s[0] == m) + (s[1] == m) + (s[2] == m);
            for (int p = 0; p < 3; p++) if (s[p] == m) share[p] += 1.0 / nw;
        }
        printf("%d %d %d %.5f %.5f %.5f\n", combo_index(c[0], c[1]), combo_index(c[2], c[3]), combo_index(c[4], c[5]),
               share[0] / nb, share[1] / nb, share[2] / nb);
    }
    return 0;
}
