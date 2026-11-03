/*
 * Solves one heads-up postflop spot (flop root) and writes what the build needs.
 *
 * usage: postflop <board> <pot> <stack> <oop.f32> <ip.f32> <out_prefix> [key=value ...]
 *   keys: donk=0|1 (OOP is not the preflop raiser: its root lead is 33% only),
 *         threads=1, target=0.5 (% of pot), maxit=1000, rake=0.10, cap=5,
 *         turnend=1 (end after the turn with an equity showdown over the river),
 *         maxraises=1, compact=2, mem_mb=0
 * Ranges: float32[1326] weights 0..1 by combo index (src/hands.py).
 *
 * Writes <out_prefix>.bin:
 *   magic "NLHPF1\0\0", int32 nodes (4), then per root node: int32 player, int32 nact,
 *   nact x int32 action code, nact x float32 amount (chips put in on the street),
 *   nact x 1326 float32 average strategy (0 for combos not in that player's range);
 *   then float32[2][1326] EV per combo (chips from the flop on, rake included),
 *   float32[2][1326] best-response EV per combo, float32[2][1326] the ranges.
 * and <out_prefix>.json with the solve statistics.
 * Root nodes: 0 OOP first action; 1 IP after OOP checks; 2 IP facing OOP's first
 * (smallest) bet; 3 OOP facing IP's first (smallest) bet after checking.
 * A missing node (for example no bet possible) is written with nact = 0.
 */
#include "cards.h"
#include "streetsolver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float *read_range(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    float *r = malloc(sizeof(float) * 1326);
    if (fread(r, sizeof(float), 1326, f) != 1326) { fprintf(stderr, "short range %s\n", path); exit(1); }
    fclose(f);
    return r;
}

static int child_with(ss_solver *S, int node, int act)
{
    if (node < 0 || ss_node_kind(S, node) != SS_NODE_DECISION) return -1;
    int n = ss_node_num_children(S, node);
    for (int i = 0; i < n; i++) {
        int c = ss_node_child(S, node, i);
        if (ss_node_action(S, c) == act) return c;
    }
    return -1;
}

static void write_node(FILE *f, ss_solver *S, int node)
{
    int32_t player = -1, nact = 0;
    if (node < 0 || ss_node_kind(S, node) != SS_NODE_DECISION) {
        fwrite(&player, 4, 1, f);
        fwrite(&nact, 4, 1, f);
        return;
    }
    player = ss_node_player(S, node);
    nact = ss_node_num_children(S, node);
    fwrite(&player, 4, 1, f);
    fwrite(&nact, 4, 1, f);
    for (int i = 0; i < nact; i++) { int32_t a = ss_node_action(S, ss_node_child(S, node, i)); fwrite(&a, 4, 1, f); }
    for (int i = 0; i < nact; i++) { float x = (float)ss_node_amount(S, ss_node_child(S, node, i)); fwrite(&x, 4, 1, f); }
    float *st = malloc(sizeof(float) * 1326 * (size_t)nact);
    ss_node_strategy(S, node, st);
    fwrite(st, sizeof(float), 1326 * (size_t)nact, f);
    free(st);
}

int main(int argc, char **argv)
{
    if (argc < 7) {
        fprintf(stderr, "usage: postflop <board> <pot> <stack> <oop.f32> <ip.f32> <out_prefix> [key=value ...]\n");
        return 2;
    }
    ss_config c;
    ss_config_default(&c);
    uint8_t board[5];
    int bl = cards_parse(argv[1], board, 5);
    if (bl != 3) { fprintf(stderr, "board must be a flop\n"); return 2; }
    memcpy(c.board, board, 3);
    c.board_len = 3;
    c.pot = atof(argv[2]);
    c.stack = atof(argv[3]);
    float *r0 = read_range(argv[4]), *r1 = read_range(argv[5]);
    c.range[0] = r0;
    c.range[1] = r1;
    const char *prefix = argv[6];
    c.num_bet_sizes = 3;
    c.bet_sizes[0] = 0.33; c.bet_sizes[1] = 0.66; c.bet_sizes[2] = 1.25;
    c.allow_allin = 1;
    c.allin_raise_only = 1;
    c.raise_multiple = 2.5;
    c.max_raises = 1;
    c.allin_threshold = 0.0;
    c.end_after_turn = 1;
    c.rake_pct = 0.10;
    c.rake_cap = 5.0;
    c.max_iterations = 1000;
    c.target_exploitability_pct = 0.5;
    c.check_every = 10;
    c.compact_storage = 2;
    int donk = 0;
    for (int i = 7; i < argc; i++) {
        char key[64];
        double val;
        if (sscanf(argv[i], "%63[^=]=%lf", key, &val) != 2) { fprintf(stderr, "bad option %s\n", argv[i]); return 2; }
        if (!strcmp(key, "donk")) donk = (int)val;
        else if (!strcmp(key, "threads")) c.threads = (int)val;
        else if (!strcmp(key, "target")) c.target_exploitability_pct = val;
        else if (!strcmp(key, "maxit")) c.max_iterations = (int)val;
        else if (!strcmp(key, "rake")) c.rake_pct = val;
        else if (!strcmp(key, "cap")) c.rake_cap = val;
        else if (!strcmp(key, "turnend")) c.end_after_turn = (int)val;
        else if (!strcmp(key, "maxraises")) c.max_raises = (int)val;
        else if (!strcmp(key, "compact")) c.compact_storage = (int)val;
        else if (!strcmp(key, "mem_mb")) c.memory_limit = (size_t)(val * 1048576.0);
        else { fprintf(stderr, "unknown option %s\n", key); return 2; }
    }
    if (donk) { c.num_donk_sizes = 1; c.donk_sizes[0] = 0.33; }
    if (c.compact_storage == 2 && !c.memory_limit) c.memory_limit = (size_t)3000 << 20;
    char err[256];
    ss_solver *S = ss_create(&c, err, sizeof err);
    if (!S) { fprintf(stderr, "postflop: %s\n", err); return 1; }
    ss_result res;
    ss_solve(S, &res);

    char path[512];
    snprintf(path, sizeof path, "%s.bin", prefix);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    fwrite("NLHPF1\0\0", 1, 8, f);
    int32_t nn = 4;
    fwrite(&nn, 4, 1, f);
    int root = ss_root(S);
    int n1 = child_with(S, root, SS_ACT_CHECK);
    int bet0 = child_with(S, root, SS_ACT_BET);          /* children are sorted by amount: first = smallest */
    int ipbet = child_with(S, n1, SS_ACT_BET);
    write_node(f, S, root);
    write_node(f, S, n1);
    write_node(f, S, bet0);
    write_node(f, S, ipbet);
    float *ev = malloc(sizeof(float) * 1326), *br = malloc(sizeof(float) * 1326);
    float evs[2][1326], brs[2][1326];
    for (int p = 0; p < 2; p++) {
        ss_root_combo_values(S, p, ev, br);
        memcpy(evs[p], ev, sizeof(float) * 1326);
        memcpy(brs[p], br, sizeof(float) * 1326);
    }
    fwrite(evs, sizeof(float), 2 * 1326, f);
    fwrite(brs, sizeof(float), 2 * 1326, f);
    fwrite(r0, sizeof(float), 1326, f);
    fwrite(r1, sizeof(float), 1326, f);
    fclose(f);

    snprintf(path, sizeof path, "%s.json", prefix);
    f = fopen(path, "w");
    fprintf(f, "{\"board\": \"%s\", \"pot\": %g, \"stack\": %g, \"donk\": %d, \"iterations\": %d, "
               "\"exploitability_pct\": [%.4f, %.4f], \"value\": [%.4f, %.4f], \"seconds\": %.1f, "
               "\"peak_memory_mb\": %.0f, \"tree_nodes\": %zu, \"compact\": %d, \"turnend\": %d, "
               "\"rake\": [%g, %g]}\n",
            argv[1], c.pot, c.stack, donk, res.iterations, res.exploitability_pct[0], res.exploitability_pct[1],
            res.value[0], res.value[1], res.seconds, res.peak_memory / 1048576.0, res.tree_nodes,
            res.compact_storage, c.end_after_turn, c.rake_pct, c.rake_cap);
    fclose(f);
    printf("%s pot %g stack %g: %d it, expl %.3f%% / %.3f%%, %.1f s, %.0f MB, %zu nodes\n", argv[1], c.pot, c.stack,
           res.iterations, res.exploitability_pct[0], res.exploitability_pct[1], res.seconds,
           res.peak_memory / 1048576.0, res.tree_nodes);
    ss_destroy(S);
    return 0;
}
