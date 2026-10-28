/*
 * Test 3: toy river game. OOP holds a polarised range (sets = nuts, queen-high =
 * air), IP holds only bluff-catchers (a pair of eights). One bet size s, which is
 * also the whole stack. At equilibrium the bluff share of OOP's betting range is
 * s / (1 + 2s) and IP calls 1 / (1 + s) of the time.
 */
#include "icmcore.h"
#include "check.h"
#include <math.h>

static float oop[NUM_COMBOS], ip[NUM_COMBOS];

static int run(double s)
{
    ss_config c;
    ss_config_default(&c);
    cards_parse("2s3h7d9cKd", c.board, 5);
    c.board_len = 5;
    c.range[0] = oop;
    c.range[1] = ip;
    c.pot = 10.0;
    c.stack = s * c.pot;
    c.num_bet_sizes = 1;
    c.bet_sizes[0] = s;
    c.max_iterations = 20000;
    c.target_exploitability_pct = 0.005;
    c.check_every = 50;

    char err[200];
    ss_solver *S = ss_create(&c, err, sizeof err);
    CHECK(S != NULL, "create: %s", err);
    if (!S) return 1;
    ss_result r;
    ss_solve(S, &r);

    int root = ss_root(S);
    int k = ss_node_num_children(S, root);
    CHECK(k == 2, "root actions %d (expected check + all-in)", k);
    static float st[8 * NUM_COMBOS];
    ss_node_strategy(S, root, st);
    int bet_child = ss_node_child(S, root, 1);
    CHECK(ss_node_action(S, bet_child) == SS_ACT_ALLIN, "second action is the bet (all-in)");

    double bet_air = 0, bet_all = 0;
    for (int i = 0; i < NUM_COMBOS; i++) {
        if (oop[i] <= 0) continue;
        int hi, lo;
        combo_cards(i, &hi, &lo);
        double b = oop[i] * st[1 * NUM_COMBOS + i];
        bet_all += b;
        if (card_rank(hi) != 5) bet_air += b;   /* rank 5 = seven */
    }
    double bluff = bet_air / bet_all;

    ss_node_strategy(S, bet_child, st);
    CHECK(ss_node_num_children(S, bet_child) == 2, "IP faces fold / call");
    CHECK(ss_node_action(S, ss_node_child(S, bet_child, 1)) == SS_ACT_CALL, "IP second action is call");
    double call = 0, wsum = 0;
    for (int i = 0; i < NUM_COMBOS; i++)
        if (ip[i] > 0) { call += ip[i] * st[1 * NUM_COMBOS + i]; wsum += ip[i]; }
    call /= wsum;

    double want_bluff = s / (1 + 2 * s), want_call = 1 / (1 + s);
    printf("s=%.2f: bluff share %.4f (theory %.4f), call %.4f (theory %.4f), iters %d, expl %.4f%% / %.4f%%\n",
           s, bluff, want_bluff, call, want_call, r.iterations, r.exploitability_pct[0], r.exploitability_pct[1]);
    CHECK(fabs(bluff - want_bluff) <= 0.01, "s=%.2f bluff share %.4f vs %.4f", s, bluff, want_bluff);
    CHECK(fabs(call - want_call) <= 0.01, "s=%.2f call frequency %.4f vs %.4f", s, call, want_call);
    ss_destroy(S);
    return 0;
}

int main(void)
{
    CHECK(range_parse("77,QJ", oop) == 0, "parse OOP range");
    CHECK(range_parse("88", ip) == 0, "parse IP range");
    run(0.33);
    run(0.75);
    run(1.5);
    return check_finish("test_toyriver");
}
