/*
 * Street solver checks: tree rules, zero-sum chip values, suit isomorphism,
 * thread determinism, cancel, memory limit and a small flop solve.
 */
#include "check.h"
#include "solver_common.h"
#include <stdio.h>
#include <string.h>

static float oop[NUM_COMBOS], ip[NUM_COMBOS];

static void base_config(ss_config *c, const char *board, double pot, double stack)
{
    ss_config_default(c);
    c->board_len = cards_parse(board, c->board, 5);
    c->range[0] = oop;
    c->range[1] = ip;
    c->pot = pot;
    c->stack = stack;
}

static ss_solver *make(const ss_config *c)
{
    char err[200];
    ss_solver *s = ss_create(c, err, sizeof err);
    CHECK(s != NULL, "create failed: %s", err);
    return s;
}

static void test_tree_rules(void)
{
    /* SPR 1.5: pot 6, stack 9. 33% = 1.98 leaves 7.02 >= 0.67 * 9.96: a bet.
       75% = 4.5 leaves 4.5 < 0.67 * 15: becomes all-in, merged with all-in. */
    range_parse("22+,A2s+,KTs+,ATo+", oop);
    range_parse("22+,A2s+,KTs+,ATo+", ip);
    ss_config c;
    base_config(&c, "Ks7d2c", 6.0, 9.0);
    c.max_iterations = 1;
    ss_solver *s = make(&c);
    if (!s) return;
    int root = ss_root(s);
    CHECK(ss_node_num_children(s, root) == 3, "SPR 1.5 root has %d actions (want check, bet 33%%, all-in)",
          ss_node_num_children(s, root));
    CHECK(ss_node_action(s, ss_node_child(s, root, 0)) == SS_ACT_CHECK, "first action check");
    CHECK(ss_node_action(s, ss_node_child(s, root, 1)) == SS_ACT_BET, "second action bet");
    CHECK(fabs(ss_node_amount(s, ss_node_child(s, root, 1)) - 1.98) < 1e-4, "bet 33%% = 1.98");
    CHECK(ss_node_action(s, ss_node_child(s, root, 2)) == SS_ACT_ALLIN, "third action all-in");
    /* Facing the 1.98 bet: fold, call, all-in (raise to 5.94 leaves 3.06 < 0.67 * 17.88). */
    int fb = ss_node_child(s, root, 1);
    CHECK(ss_node_num_children(s, fb) == 3, "facing bet: %d actions", ss_node_num_children(s, fb));
    CHECK(ss_node_action(s, ss_node_child(s, fb, 0)) == SS_ACT_FOLD, "fold first");
    CHECK(ss_node_action(s, ss_node_child(s, fb, 2)) == SS_ACT_ALLIN, "raise became all-in");
    /* After bet-call the turn is a chance node with 49 cards; rainbow flop with
       ranges symmetric only under no suit swap -> 49 children. */
    int call = ss_node_child(s, fb, 1);
    CHECK(ss_node_kind(s, call) == SS_NODE_CHANCE, "call leads to the turn");
    ss_destroy(s);

    /* Deep: pot 6, stack 36. Root: check, 1.98, 4.5, all-in. Facing 4.5: raise to 13.5
       leaves 22.5 >= 0.67 * 33 = 22.11 -> a real raise; then only fold / call / all-in. */
    base_config(&c, "Ks7d2c", 6.0, 36.0);
    c.max_iterations = 1;
    c.memory_limit = 1;   /* rejected: proves the estimate and the limit work */
    char err[200];
    s = ss_create(&c, err, sizeof err);
    CHECK(s == NULL && strstr(err, "limit"), "memory limit not enforced (%s)", err);
    ss_destroy(s);
}

static void test_river_properties(void)
{
    range_parse("22+,A2s+,K2s+,Q6s+,J7s+,T7s+,97s+,87s,76s,65s,A2o+,K8o+,Q9o+,J9o+,T9o", oop);
    range_parse("44+,A5s+,K9s+,QTs+,JTs,ATo+,KJo+,QJo", ip);
    ss_config c;
    base_config(&c, "Ah9d6c3s2d", 20.0, 60.0);
    c.max_iterations = 2000;
    c.target_exploitability_pct = 0.2;
    ss_solver *a = make(&c);
    c.threads = 4;
    ss_solver *b = make(&c);
    if (!a || !b) return;
    ss_result ra, rb;
    ss_solve(a, &ra);
    ss_solve(b, &rb);
    printf("river: %d iters, expl %.3f%% / %.3f%%, %zu nodes, %.2f MB, %.3f s\n", ra.iterations,
           ra.exploitability_pct[0], ra.exploitability_pct[1], ra.tree_nodes, ra.peak_memory / 1048576.0, ra.seconds);
    CHECK(ra.exploitability_pct[0] <= 0.2 && ra.exploitability_pct[1] <= 0.2, "river did not converge");
    CHECK(fabs(ra.value[0] + ra.value[1] - c.pot) < 1e-3 * c.pot, "chip values not zero-sum: %.5f + %.5f vs pot %.1f",
          ra.value[0], ra.value[1], c.pot);
    CHECK(ra.iterations == rb.iterations && ra.value[0] == rb.value[0], "1 vs 4 threads differ on a river (no chance nodes)");
    ss_destroy(a);
    ss_destroy(b);
}

static void test_isomorphism_and_threads(void)
{
    /* Turn board with spades and one heart: clubs and diamonds are interchangeable. */
    range_parse("66+,A9s+,KTs+,QTs+,JTs,T9s,98s,AJo+,KQo", oop);
    range_parse("88+,ATs+,KJs+,QJs,AQo+", ip);
    ss_config c;
    base_config(&c, "Ks9s4s2h", 10.0, 30.0);
    c.max_iterations = 1000;
    c.target_exploitability_pct = 0.15;
    c.check_every = 10;
    ss_solver *iso = make(&c);
    c.use_isomorphism = 0;
    ss_solver *plain = make(&c);
    c.use_isomorphism = 1;
    c.threads = 4;
    ss_solver *par = make(&c);
    if (!iso || !plain || !par) return;
    ss_result ri, rp, rq;
    ss_solve(iso, &ri);
    ss_solve(plain, &rp);
    ss_solve(par, &rq);
    printf("turn iso: %zu nodes %.3f s value %.4f expl %.3f%%; no iso: %zu nodes %.3f s value %.4f expl %.3f%%; 4 threads %.3f s\n",
           ri.tree_nodes, ri.seconds, ri.value[0], ri.exploitability_pct[0], rp.tree_nodes, rp.seconds, rp.value[0],
           rp.exploitability_pct[0], rq.seconds);
    CHECK(ri.tree_nodes < rp.tree_nodes, "isomorphism did not shrink the tree");
    CHECK(fabs(ri.value[0] - rp.value[0]) < 0.002 * c.pot, "iso value %.5f vs %.5f", ri.value[0], rp.value[0]);
    for (int p = 0; p < 2; p++)
        CHECK(ri.exploitability_pct[p] <= 0.15 && rp.exploitability_pct[p] <= 0.15, "iso / plain did not converge");
    CHECK(fabs(ri.value[0] - rq.value[0]) < 1e-6 * c.pot && ri.exploitability_pct[1] == rq.exploitability_pct[1],
          "threads change the result: %.7f vs %.7f", ri.value[0], rq.value[0]);
    /* An isomorphic card maps onto a stored child. */
    int root = ss_root(iso);
    int cc = ss_node_child(iso, ss_node_child(iso, root, 0), 0);   /* check, check -> river chance */
    CHECK(ss_node_kind(iso, cc) == SS_NODE_CHANCE, "check-check leads to the river");
    uint8_t perm[4];
    int a = ss_chance_child(iso, cc, card_parse("7c"), perm);
    int b = ss_chance_child(iso, cc, card_parse("7d"), perm);
    CHECK(a >= 0 && a == b, "7c and 7d share a child");
    CHECK(ss_chance_child(iso, cc, card_parse("Ks"), perm) == -1, "board card has no child");
    ss_destroy(iso);
    ss_destroy(plain);
    ss_destroy(par);
}

static void test_compact_storage(void)
{
    range_parse("66+,A9s+,KTs+,QTs+,JTs,T9s,98s,AJo+,KQo", oop);
    range_parse("88+,ATs+,KJs+,QJs,AQo+", ip);
    ss_config c;
    base_config(&c, "Ks9s4s2h", 10.0, 30.0);
    c.max_iterations = 1000;
    c.target_exploitability_pct = 0.15;
    ss_solver *f = make(&c);
    c.compact_storage = 1;
    ss_solver *q = make(&c);
    if (!f || !q) return;
    ss_result rf, rq;
    ss_solve(f, &rf);
    ss_solve(q, &rq);
    printf("compact storage: %.2f MB vs %.2f MB, %d vs %d iterations, value %.4f vs %.4f\n", rq.peak_memory / 1048576.0,
           rf.peak_memory / 1048576.0, rq.iterations, rf.iterations, rq.value[0], rf.value[0]);
    CHECK(rq.compact_storage == 1 && rf.compact_storage == 0, "storage mode not reported");
    CHECK(rq.peak_memory < rf.peak_memory, "compact storage uses more memory");
    CHECK(rq.exploitability_pct[0] <= 0.15 && rq.exploitability_pct[1] <= 0.15, "compact storage did not converge");
    CHECK(fabs(rq.value[0] - rf.value[0]) < 0.003 * c.pot, "compact value %.5f vs %.5f", rq.value[0], rf.value[0]);
    /* auto mode switches to 16-bit only when float32 would exceed the limit */
    c.compact_storage = 2;
    c.memory_limit = (size_t)(rf.peak_memory * 0.8);
    ss_solver *a = make(&c);
    if (a) {
        ss_result ra;
        c.max_iterations = 1;
        ss_evaluate(a, &ra);
        CHECK(ra.compact_storage == 1 && ra.peak_memory <= c.memory_limit, "auto mode did not pick 16-bit storage");
    }
    ss_destroy(a);
    ss_destroy(f);
    ss_destroy(q);
}

static void test_warm_start(void)
{
    range_parse("66+,A9s+,KTs+,QTs+,JTs,T9s,98s,AJo+,KQo", oop);
    range_parse("88+,ATs+,KJs+,QJs,AQo+", ip);
    ss_config c;
    base_config(&c, "Ks9s4s2h", 10.0, 40.0);
    c.max_iterations = 1000;
    c.target_exploitability_pct = 0.3;
    ss_solver *chips = make(&c);
    c.mode = SS_MODE_ICM;
    c.icm_players = 4;
    c.icm_stacks[0] = 45; c.icm_stacks[1] = 60; c.icm_stacks[2] = 30; c.icm_stacks[3] = 15;
    c.icm_num_payouts = 3;
    c.icm_payouts[0] = 50; c.icm_payouts[1] = 30; c.icm_payouts[2] = 20;
    ss_solver *cold = make(&c), *warm = make(&c);
    if (!chips || !cold || !warm) return;
    ss_result r0, rc, rw;
    ss_solve(chips, &r0);
    ss_solve(cold, &rc);
    CHECK(ss_warm_start(warm, chips, 1.0) == 0, "warm start");
    ss_solve(warm, &rw);
    printf("warm start: ICM cold %d iterations, warm from chip EV %d iterations\n", rc.iterations, rw.iterations);
    CHECK(rw.exploitability_pct[0] <= 0.3 && rw.exploitability_pct[1] <= 0.3, "warm start did not converge");
    CHECK(rw.iterations <= rc.iterations, "warm start slower than cold (%d vs %d)", rw.iterations, rc.iterations);
    CHECK(fabs(rw.value[0] - rc.value[0]) < 0.01 * rc.pot_value[0], "warm value %.5f vs cold %.5f", rw.value[0], rc.value[0]);
    /* the same warm start through a state file */
    char path[] = "test_solver_state.tmp";
    ss_solver *viafile = make(&c);
    if (viafile) {
        CHECK(ss_save_state(chips, path) == 0, "save state");
        CHECK(ss_load_state(viafile, path) == 0, "load state");
        ss_restart_average(viafile, 1.0);
        ss_result rf;
        ss_solve(viafile, &rf);
        CHECK(rf.iterations == rw.iterations && rf.value[0] == rw.value[0], "warm start via file differs (%d vs %d)",
              rf.iterations, rw.iterations);
        CHECK(ss_load_state(cold, "no_such_state_file.tmp") == -1, "loading a missing file must fail");
        ss_destroy(viafile);
        remove(path);
    }
    ss_destroy(chips);
    ss_destroy(cold);
    ss_destroy(warm);
}

static int cancel_after = 0;
static void progress_cb(void *user, int iteration, double expl)
{
    (void)expl;
    if (iteration >= cancel_after) ss_cancel(*(ss_solver **)user);
}

static void test_cancel(void)
{
    range_parse("22+,A2s+,KTs+,ATo+", oop);
    range_parse("22+,A2s+,KTs+,ATo+", ip);
    ss_config c;
    base_config(&c, "Ah9d6c3s2d", 20.0, 60.0);
    c.max_iterations = 100000;
    c.target_exploitability_pct = 0.0;
    c.check_every = 5;
    c.progress = progress_cb;
    cancel_after = 20;
    static ss_solver *s;
    c.progress_user = &s;
    s = make(&c);
    if (!s) return;
    ss_result r;
    int st = ss_solve(s, &r);
    CHECK(st == SS_CANCELLED && r.iterations == 20, "cancel: status %d after %d iterations", st, r.iterations);
    CHECK(ss_progress_iterations(s) == 20, "progress iterations %d", ss_progress_iterations(s));
    ss_destroy(s);
}

static void test_flop(void)
{
    range_parse("TT+,AJs+,KQs,AQo+", oop);
    range_parse("99+,ATs+,KJs+,AJo+,KQo", ip);
    ss_config c;
    base_config(&c, "Ks7d2c", 6.0, 9.0);
    c.max_iterations = 300;
    c.target_exploitability_pct = 0.5;
    c.threads = 4;
    ss_solver *s = make(&c);
    if (!s) return;
    ss_result r;
    ss_solve(s, &r);
    printf("flop SPR 1.5 (small ranges): %d iters, expl %.3f%% / %.3f%%, %zu nodes, %.1f MB, %.2f s\n", r.iterations,
           r.exploitability_pct[0], r.exploitability_pct[1], r.tree_nodes, r.peak_memory / 1048576.0, r.seconds);
    CHECK(r.exploitability_pct[0] <= 0.5 && r.exploitability_pct[1] <= 0.5, "flop did not converge");
    CHECK(fabs(r.value[0] + r.value[1] - c.pot) < 2e-3 * c.pot, "flop values not zero-sum");
    double f[16];
    int k = action_freqs(s, ss_root(s), oop, f);
    printf("  OOP root:");
    for (int a = 0; a < k; a++) printf(" %s %.1f%%", action_name(ss_node_action(s, ss_node_child(s, 0, a))), 100 * f[a]);
    printf("\n");
    ss_destroy(s);
}

int main(void)
{
    test_tree_rules();
    test_river_properties();
    test_isomorphism_and_threads();
    test_compact_storage();
    test_warm_start();
    test_cancel();
    test_flop();
    return check_finish("test_solver");
}
