/* Rake in the postflop solver (stage 2): showdown and fold payoffs with rake. */
#include "cards.h"
#include "streetsolver.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;

static double solve_value(const char *board, const char *oop, const char *ip, double pot, double stack, int who)
{
    static float r0[1326], r1[1326];
    memset(r0, 0, sizeof r0);
    memset(r1, 0, sizeof r1);
    uint8_t b[5], h[2];
    cards_parse(oop, h, 2);
    r0[combo_index(h[0], h[1])] = 1;
    cards_parse(ip, h, 2);
    r1[combo_index(h[0], h[1])] = 1;
    ss_config c;
    ss_config_default(&c);
    c.board_len = cards_parse(board, b, 5);
    memcpy(c.board, b, 5);
    c.range[0] = r0;
    c.range[1] = r1;
    c.pot = pot;
    c.stack = stack;
    c.rake_pct = 0.10;
    c.rake_cap = 5.0;
    c.max_iterations = 300;
    c.target_exploitability_pct = 0.01;
    char err[200];
    ss_solver *S = ss_create(&c, err, sizeof err);
    if (!S) { printf("create failed: %s\n", err); fails++; return -1; }
    ss_result r;
    ss_solve(S, &r);
    double v = r.value[who];
    ss_destroy(S);
    return v;
}

static void check(const char *name, double got, double want, double tol)
{
    int ok = fabs(got - want) <= tol;
    fails += !ok;
    printf("%s %s: %.4f (want %.4f)\n", ok ? "ok  " : "FAIL", name, got, want);
}

int main(void)
{
    check("showdown pot 20: winner gets 18", solve_value("2c7d9hJsKc", "KsKd", "QhQd", 20, 0, 0), 18.0, 1e-3);
    check("showdown pot 20: loser 0", solve_value("2c7d9hJsKc", "KsKd", "QhQd", 20, 0, 1), 0.0, 1e-3);
    check("showdown pot 80: cap 5", solve_value("2c7d9hJsKc", "KsKd", "QhQd", 80, 0, 0), 75.0, 1e-3);
    check("tie pot 60: (60 - 5) / 2", solve_value("AhKdQcJsTh", "2c3d", "2d3h", 60, 0, 0), 27.5, 1e-3);
    check("river vs air, stack 100: OOP keeps pot - rake(20)", solve_value("2c7d9hJsKc", "KsKd", "8c3h", 20, 100, 0), 18.0, 0.05);
    printf("test_rake: %s\n", fails ? "FAILED" : "PASS");
    return fails != 0;
}
