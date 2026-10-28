/* Test 2: ICM matches a brute-force Malmuth-Harville calculation; equities sum to the pool. */
#include "icmcore.h"
#include "check.h"
#include <math.h>

/* Brute force: sum over every complete finishing order of its probability. */
static void orders(int n, const double *s, const double *pay, double *eq,
                   int *used, int place, double prob, double rest)
{
    if (place == n) return;
    for (int i = 0; i < n; i++) {
        if (used[i]) continue;
        double q = prob * s[i] / rest;
        eq[i] += q * pay[place];
        used[i] = 1;
        orders(n, s, pay, eq, used, place + 1, q, rest - s[i]);
        used[i] = 0;
    }
}

static void brute_force(int n, const double *s, const double *pay, double *eq)
{
    int used[ICM_MAX_PLAYERS] = { 0 };
    double total = 0.0;
    for (int i = 0; i < n; i++) { total += s[i]; eq[i] = 0.0; }
    orders(n, s, pay, eq, used, 0, 1.0, total);
}

static double frand(unsigned *state)
{
    *state = *state * 1103515245u + 12345u;
    return ((*state >> 8) & 0xffffff) / 16777216.0;
}

int main(void)
{
    unsigned rng = 12345;
    int tables = 0;
    double worst = 0.0;
    for (int n = 2; n <= ICM_MAX_PLAYERS; n++) {
        int reps = n <= 7 ? 200 : 20;
        for (int t = 0; t < reps; t++) {
            double s[ICM_MAX_PLAYERS], pay[ICM_MAX_PLAYERS], eq[ICM_MAX_PLAYERS], bf[ICM_MAX_PLAYERS];
            int npay = 1 + (int)(frand(&rng) * n);
            double pool = 0.0;
            for (int i = 0; i < n; i++) s[i] = 1.0 + floor(frand(&rng) * 200.0);
            /* Decreasing payouts, random shape. */
            double v = 100.0 + frand(&rng) * 900.0;
            for (int k = 0; k < n; k++) {
                pay[k] = k < npay ? v : 0.0;
                pool += pay[k];
                v *= 0.3 + 0.7 * frand(&rng);
            }
            CHECK(icm_equity(n, s, npay, pay, eq) == 0, "icm_equity failed n=%d", n);
            brute_force(n, s, pay, bf);
            double sum = 0.0;
            for (int i = 0; i < n; i++) {
                double d = fabs(eq[i] - bf[i]);
                if (d > worst) worst = d;
                CHECK(d <= 1e-9 * pool, "n=%d player %d: dp %.12f brute %.12f", n, i, eq[i], bf[i]);
                sum += eq[i];
            }
            CHECK(fabs(sum - pool) <= 1e-9 * pool, "n=%d sum %.12f pool %.12f", n, sum, pool);
            tables++;
        }
    }

    /* Known values: two players, 2:1 chips, payouts 70/30 -> 0.667*70+0.333*30. */
    {
        double s[2] = { 2, 1 }, pay[2] = { 70, 30 }, eq[2];
        icm_equity(2, s, 2, pay, eq);
        CHECK(fabs(eq[0] - (2.0 / 3 * 70 + 1.0 / 3 * 30)) < 1e-12, "2-player value %.6f", eq[0]);
    }
    /* Zero stacks share the bottom places; equities still sum to the pool. */
    {
        double s[4] = { 50, 0, 30, 0 }, pay[4] = { 50, 30, 15, 5 }, eq[4];
        CHECK(icm_equity(4, s, 4, pay, eq) == 0, "zero-stack call failed");
        CHECK(fabs(eq[1] - 10.0) < 1e-12 && fabs(eq[3] - 10.0) < 1e-12, "zero stacks %.4f %.4f", eq[1], eq[3]);
        CHECK(fabs(eq[0] + eq[2] - 80.0) < 1e-12, "live players %.4f", eq[0] + eq[2]);
    }
    /* Winner-take-all is proportional to chips. */
    {
        double s[3] = { 10, 20, 70 }, pay[1] = { 1000 }, eq[3];
        icm_equity(3, s, 1, pay, eq);
        CHECK(fabs(eq[2] - 700.0) < 1e-9, "winner-take-all %.6f", eq[2]);
    }
    /* Bad input. */
    {
        double s[2] = { 10, -1 }, pay[1] = { 1 }, eq[2];
        CHECK(icm_equity(2, s, 1, pay, eq) == -1, "negative stack accepted");
        CHECK(icm_equity(10, s, 1, pay, eq) == -1, "10 players accepted");
    }
    printf("ICM: %d random tables (2-9 players), worst abs diff %.3g\n", tables, worst);
    return check_finish("test_icm");
}
