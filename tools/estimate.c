/*
 * Early compute estimate: one flop solve at SPR 1.5 and one at SPR 6 in each pot
 * type (placeholder ranges, flop Ks7d2c, chip EV mode: ICM payoffs cost the same
 * per iteration). For each spot: tree size, solver memory, single-threaded time
 * per iteration (including the exploitability checks every 10 iterations), and a
 * full solve with N threads to the 0.5% target (cap 1000 iterations).
 *   usage: estimate [threads] [flop] [spot ...]   spot = srp1.5 srp6 3bp1.5 3bp6
 * Writes a summary line per spot to stdout (and the progress to stderr).
 */
#include "tool_common.h"
#include <time.h>

typedef struct { const char *id, *file; double pot, spr; } Spot;

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

static void progress(void *user, int it, double e)
{
    fprintf(stderr, "  %s: iteration %d, exploitability %.3f%%\n", (const char *)user, it, e);
}

static int run(const Spot *sp, const char *flop, int threads)
{
    static float oop[NUM_COMBOS], ip[NUM_COMBOS];
    if (load_range(sp->file, "bb", oop) || load_range(sp->file, "btn", ip)) return 1;
    ss_config c;
    ss_config_default(&c);
    c.board_len = cards_parse(flop, c.board, 5);
    c.range[0] = oop;
    c.range[1] = ip;
    c.pot = sp->pot;
    c.stack = sp->spr * sp->pot;
    c.max_iterations = 10;
    c.target_exploitability_pct = 0.0;
    c.check_every = 10;
    char err[256];

    /* single-threaded cost of 10 iterations + 1 check */
    double t0 = now();
    ss_solver *s = ss_create(&c, err, sizeof err);
    if (!s) { fprintf(stderr, "%s: %s\n", sp->id, err); return 1; }
    double tcreate = now() - t0;
    size_t mem = ss_memory_required(s);
    ss_result r;
    ss_solve(s, &r);
    double per_iter_1t = r.seconds / r.iterations;
    size_t nodes = r.tree_nodes;
    ss_destroy(s);
    fprintf(stderr, "%s: %zu nodes, %.1f MB, build %.1f s, single-thread %.3f s/iteration\n", sp->id, nodes,
            mem / 1048576.0, tcreate, per_iter_1t);

    /* full solve */
    c.max_iterations = 1000;
    c.target_exploitability_pct = 0.5;
    c.threads = threads;
    c.progress = progress;
    c.progress_user = (void *)sp->id;
    s = ss_create(&c, err, sizeof err);
    if (!s) { fprintf(stderr, "%s: %s\n", sp->id, err); return 1; }
    ss_solve(s, &r);
    const uint16_t *cb;
    int n0 = ss_player_combos(s, 0, &cb), n1 = ss_player_combos(s, 1, &cb);
    ss_destroy(s);
    printf("%-7s flop %s pot %4.1f stack %5.1f combos %3d/%3d | nodes %9zu | memory %7.1f MB | build %5.1f s | "
           "1-thread %7.3f s/iter | %d threads: %4d iterations, %8.1f s, expl %.3f%% / %.3f%% | proj. 1-thread solve %8.0f s | peak RSS %.0f MB\n",
           sp->id, flop, c.pot, c.stack, n0, n1, nodes, mem / 1048576.0, tcreate, per_iter_1t, threads, r.iterations,
           r.seconds, r.exploitability_pct[0], r.exploitability_pct[1], per_iter_1t * r.iterations + tcreate, peak_rss_mb());
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    static const Spot spots[] = {
        { "srp1.5", "ranges/srp_spr1.5.json", 6.0, 1.5 },
        { "srp6", "ranges/srp_spr6.json", 6.0, 6.0 },
        { "3bp1.5", "ranges/3bp_spr1.5.json", 15.0, 1.5 },
        { "3bp6", "ranges/3bp_spr6.json", 15.0, 6.0 },
    };
    int threads = argc > 1 ? atoi(argv[1]) : 4;
    const char *flop = argc > 2 ? argv[2] : "Ks7d2c";
    handeval_init();
    for (int i = 0; i < 4; i++) {
        int want = argc <= 3;
        for (int a = 3; a < argc; a++) want |= !strcmp(argv[a], spots[i].id);
        if (want) run(&spots[i], flop, threads);
    }
    return 0;
}
