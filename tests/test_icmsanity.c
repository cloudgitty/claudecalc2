/*
 * Test 4: with winner-take-all payouts ICM equity is proportional to chips, so
 * ICM mode must match chip-EV mode within solver tolerance. Also shows that with
 * the spec's 40/25/15/10/6/4 payouts the strategy does change.
 */
#include "check.h"
#include "solver_common.h"
#include <stdlib.h>

static float oop[NUM_COMBOS], ip[NUM_COMBOS];

static ss_solver *solve(int mode, int npay, const double *pay, ss_result *r)
{
    ss_config c;
    ss_config_default(&c);
    c.board_len = cards_parse("Ks7d2c4h", c.board, 5);
    c.range[0] = oop;
    c.range[1] = ip;
    c.pot = 10.0;
    c.stack = 25.0;
    c.mode = mode;
    c.icm_players = 4;
    double stacks[4] = { 35, 25, 60, 20 };   /* behind, seats 0 and 1 play */
    for (int i = 0; i < 4; i++) c.icm_stacks[i] = stacks[i];
    c.seat[0] = 0;
    c.seat[1] = 1;
    c.icm_num_payouts = npay;
    for (int i = 0; i < npay; i++) c.icm_payouts[i] = pay[i];
    c.max_iterations = getenv("ITERS") ? atoi(getenv("ITERS")) : 300;
    c.target_exploitability_pct = getenv("TARGET") ? atof(getenv("TARGET")) : 0.3;
    c.threads = 4;
    char err[200];
    ss_solver *S = ss_create(&c, err, sizeof err);
    CHECK(S != NULL, "create: %s", err);
    if (S) ss_solve(S, r);
    return S;
}

int main(void)
{
    CHECK(range_parse("22+,A2s+,K5s+,Q8s+,J8s+,T8s+,97s+,86s+,75s+,64s+,54s,A7o+,KTo+,QTo+,JTo", oop) == 0, "oop range");
    CHECK(range_parse("55+,A8s+,KTs+,QJs,AJo+,KQo,76s,65s", ip) == 0, "ip range");
    double wta[1] = { 100 };
    double pay[6] = { 40, 25, 15, 10, 6, 4 };

    ss_result rc, ri, rp;
    ss_solver *chips = solve(SS_MODE_CHIPS, 1, wta, &rc);
    ss_solver *icm = solve(SS_MODE_ICM, 1, wta, &ri);
    ss_solver *real = solve(SS_MODE_ICM, 6, pay, &rp);
    if (!chips || !icm || !real) return check_finish("test_icmsanity");

    printf("chips: %d iters, expl %.3f%% / %.3f%%, value %.4f / %.4f of pot\n", rc.iterations,
           rc.exploitability_pct[0], rc.exploitability_pct[1], rc.value[0] / rc.pot_value[0], rc.value[1] / rc.pot_value[1]);
    printf("ICM winner-take-all: %d iters, expl %.3f%% / %.3f%%, value %.4f / %.4f of pot\n", ri.iterations,
           ri.exploitability_pct[0], ri.exploitability_pct[1], ri.value[0] / ri.pot_value[0], ri.value[1] / ri.pot_value[1]);
    printf("ICM 40/25/15/10/6/4: %d iters, expl %.3f%% / %.3f%%\n", rp.iterations,
           rp.exploitability_pct[0], rp.exploitability_pct[1]);

    /* The ICM (winner-take-all) strategy, played in the chip-EV game, must be as
       unexploitable as the chip-EV solution itself (within the solver target). */
    {
        ss_result rx;
        ss_solver *probe = solve(SS_MODE_CHIPS, 1, wta, &rx);
        CHECK(ss_copy_strategy(probe, icm) == 0, "copy strategy");
        ss_evaluate(probe, &rx);
        printf("ICM winner-take-all strategy evaluated in the chip-EV game: expl %.3f%% / %.3f%% of pot\n",
               rx.exploitability_pct[0], rx.exploitability_pct[1]);
        for (int p = 0; p < 2; p++)
            CHECK(rx.exploitability_pct[p] <= 0.5, "player %d: ICM strategy exploitable by %.3f%% in chip game", p,
                  rx.exploitability_pct[p]);
        ss_result ry;
        CHECK(ss_copy_strategy(probe, real) == 0, "copy strategy");
        ss_evaluate(probe, &ry);
        printf("for contrast, the 40/25/15/10/6/4 ICM strategy in the chip-EV game: expl %.3f%% / %.3f%%\n",
               ry.exploitability_pct[0], ry.exploitability_pct[1]);
        ss_destroy(probe);
    }

    /* Strategies of indifferent or barely reached combos are not unique, so compare
       reach-weighted strategies at every node reached with at least 2% of a range. */
    int nodes = 0;
    double d = reach_weighted_diff(chips, icm, oop, ip, 0.02, &nodes);
    printf("worst reach-weighted strategy distance chips vs ICM winner-take-all over %d reached nodes: %.4f\n", nodes, d);
    CHECK(d < 0.10, "strategies differ by %.4f", d);
    int nodes_real = 0;
    double dr = reach_weighted_diff(chips, real, oop, ip, 0.02, &nodes_real);
    printf("same distance chips vs ICM 40/25/15/10/6/4 (for contrast): %.4f\n", dr);
    for (int p = 0; p < 2; p++) {
        double vc = rc.value[p] / rc.pot_value[p], vi = ri.value[p] / ri.pot_value[p];
        CHECK(fabs(vc - vi) < 0.005, "player %d value %.5f vs %.5f (fraction of pot)", p, vc, vi);
        CHECK(fabs(rc.exploitability_pct[p] - ri.exploitability_pct[p]) < 0.05, "player %d exploitability differs", p);
    }
    CHECK(rc.iterations == ri.iterations, "iterations %d vs %d", rc.iterations, ri.iterations);

    double fc[16], fp[16];
    int root = ss_root(chips);
    int k = action_freqs(chips, root, oop, fc);
    action_freqs(real, root, oop, fp);
    printf("OOP root:");
    for (int a = 0; a < k; a++)
        printf("  %s %.0f%%: chips %.1f%% / ICM %.1f%%", action_name(ss_node_action(chips, ss_node_child(chips, root, a))),
               100 * ss_node_amount(chips, ss_node_child(chips, root, a)) / 10.0, 100 * fc[a], 100 * fp[a]);
    printf("\n");

    ss_destroy(chips);
    ss_destroy(icm);
    ss_destroy(real);
    return check_finish("test_icmsanity");
}
