/*
 * Benchmarks for on-device use: a river solve and a turn-plus-river solve, with
 * 1 and 4 threads, stop rule 0.5% of the pot (cap 1000 iterations).
 *   usage: bench [ranges.json] [river|turn|all]
 * Default ranges: ranges/srp_spr6.json (widest placeholder ranges, a worst case:
 * ranges reaching the turn or river in practice are narrower).
 */
#include "tool_common.h"

typedef struct { const char *name, *board; double pot, stack; } Spot;

static int run(const Spot *sp, const float *oop, const float *ip, int threads)
{
    ss_config c;
    ss_config_default(&c);
    c.board_len = cards_parse(sp->board, c.board, 5);
    c.range[0] = oop;
    c.range[1] = ip;
    c.pot = sp->pot;
    c.stack = sp->stack;
    c.threads = threads;
    c.max_iterations = 1000;
    c.target_exploitability_pct = 0.5;
    c.check_every = 10;
    c.compact_storage = getenv("COMPACT") ? atoi(getenv("COMPACT")) : 0;
    char err[256];
    ss_solver *s = ss_create(&c, err, sizeof err);
    if (!s) { fprintf(stderr, "%s: %s\n", sp->name, err); return 1; }
    ss_result r;
    ss_solve(s, &r);
    const uint16_t *cb;
    int n0 = ss_player_combos(s, 0, &cb), n1 = ss_player_combos(s, 1, &cb);
    printf("%-6s %-12s pot %5.1f stack %5.1f combos %4d/%4d threads %d: %7.3f s, %4d iterations, expl %.3f%% / %.3f%%, "
           "%zu nodes, %.1f MB%s\n",
           sp->name, sp->board, sp->pot, sp->stack, n0, n1, threads, r.seconds, r.iterations, r.exploitability_pct[0],
           r.exploitability_pct[1], r.tree_nodes, r.peak_memory / 1048576.0, r.compact_storage ? " (16-bit storage)" : "");
    fflush(stdout);
    ss_destroy(s);
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "ranges/srp_spr6.json";
    const char *which = argc > 2 ? argv[2] : "all";
    static float oop[NUM_COMBOS], ip[NUM_COMBOS];
    if (load_range(path, "bb", oop) || load_range(path, "btn", ip)) return 1;
    handeval_init();
    const Spot river = { "river", "Ks7d2c4h9s", 20.0, 60.0 };
    const Spot turn = { "turn", "Ks7d2c4h", 10.0, 50.0 };
    printf("ranges: %s (OOP = bb, IP = btn)\n", path);
    if (!strcmp(which, "river") || !strcmp(which, "all")) {
        run(&river, oop, ip, 1);
        run(&river, oop, ip, 4);
    }
    if (!strcmp(which, "turn") || !strcmp(which, "all")) {
        run(&turn, oop, ip, 1);
        run(&turn, oop, ip, 4);
    }
    return 0;
}
