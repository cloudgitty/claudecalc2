#include "icm.h"

/*
 * Subset dynamic programme over the players with chips. prob[mask] is the
 * probability that exactly the players in mask occupy the top popcount(mask)
 * places. Each step picks the next finisher from the rest in proportion to
 * stack size, which is the Malmuth-Harville model. O(2^m * m) for m players.
 */
int icm_equity(int n, const double *stacks, int npay, const double *payouts, double *equity)
{
    if (n < 1 || n > ICM_MAX_PLAYERS || !stacks || !equity || npay < 0 || (npay > 0 && !payouts))
        return -1;

    double pay[ICM_MAX_PLAYERS];
    for (int k = 0; k < n; k++) pay[k] = k < npay ? payouts[k] : 0.0;

    int live[ICM_MAX_PLAYERS], m = 0;
    double s[ICM_MAX_PLAYERS], total = 0.0;
    for (int i = 0; i < n; i++) {
        if (!(stacks[i] >= 0.0)) return -1;
        equity[i] = 0.0;
        if (stacks[i] > 0.0) {
            live[m] = i;
            s[m] = stacks[i];
            total += stacks[i];
            m++;
        }
    }

    if (m > 0) {
        double prob[1 << ICM_MAX_PLAYERS];
        double rest[1 << ICM_MAX_PLAYERS];
        unsigned full = (1u << m) - 1u;
        prob[0] = 1.0;
        rest[0] = total;
        for (unsigned mask = 1; mask <= full; mask++) {
            prob[mask] = 0.0;
            int low = __builtin_ctz(mask);
            rest[mask] = rest[mask & (mask - 1)] - s[low];
        }
        for (unsigned mask = 0; mask < full; mask++) {
            double p = prob[mask];
            if (p == 0.0) continue;
            double r = rest[mask];
            double place_pay = pay[__builtin_popcount(mask)];
            for (int j = 0; j < m; j++) {
                unsigned bit = 1u << j;
                if (mask & bit) continue;
                double q = p * s[j] / r;
                prob[mask | bit] += q;
                equity[live[j]] += q * place_pay;
            }
        }
    }

    int zeros = n - m;
    if (zeros > 0) {
        double shared = 0.0;
        for (int k = m; k < n; k++) shared += pay[k];
        shared /= (double)zeros;
        for (int i = 0; i < n; i++)
            if (stacks[i] == 0.0) equity[i] = shared;
    }
    return 0;
}
