#ifndef TESTS_SOLVER_COMMON_H
#define TESTS_SOLVER_COMMON_H

#include "icmcore.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>


/* Range-weighted frequency of each action at a node (out[a], sums to 1). */
static inline int action_freqs(const ss_solver *s, int node, const float *range, double *out)
{
    static float st[16 * NUM_COMBOS];
    int k = ss_node_strategy(s, node, st);
    double tot = 0;
    for (int a = 0; a < k; a++) out[a] = 0;
    for (int i = 0; i < NUM_COMBOS; i++) {
        if (range[i] <= 0) continue;
        tot += range[i];
        for (int a = 0; a < k; a++) out[a] += range[i] * st[a * NUM_COMBOS + i];
    }
    for (int a = 0; a < k; a++) out[a] /= tot;
    return k;
}

static inline const char *action_name(int act)
{
    switch (act) {
    case SS_ACT_FOLD: return "fold";
    case SS_ACT_CHECK: return "check";
    case SS_ACT_CALL: return "call";
    case SS_ACT_BET: return "bet";
    case SS_ACT_RAISE: return "raise";
    case SS_ACT_ALLIN: return "allin";
    default: return "?";
    }
}

#endif

/* Reach-weighted strategy difference: at every decision node whose actor reach
   is at least min_rel of the starting range, the mean over the actor's combos
   (weighted by reach under solver a) of the total-variation distance between the
   two strategies. Returns the worst node. */
typedef struct { const ss_solver *a, *b; double min_rel, worst; int nodes; double root_mass[2]; } ReachCmp;

static inline void reach_cmp_walk(ReachCmp *R, int na, int nb, float *reach0, float *reach1)
{
    static float sa[16 * NUM_COMBOS], sb[16 * NUM_COMBOS];
    int kind = ss_node_kind(R->a, na);
    int k = ss_node_num_children(R->a, na);
    if (kind == SS_NODE_CHANCE) {
        for (int i = 0; i < k; i++)
            reach_cmp_walk(R, ss_node_child(R->a, na, i), ss_node_child(R->b, nb, i), reach0, reach1);
        return;
    }
    if (kind != SS_NODE_DECISION) return;
    int p = ss_node_player(R->a, na);
    float *rp = p == 0 ? reach0 : reach1;
    ss_node_strategy(R->a, na, sa);
    ss_node_strategy(R->b, nb, sb);
    double mass = 0, diff = 0;
    for (int i = 0; i < NUM_COMBOS; i++) {
        if (rp[i] <= 0) continue;
        double tv = 0;
        for (int x = 0; x < k; x++) tv += fabs(sa[x * NUM_COMBOS + i] - sb[x * NUM_COMBOS + i]);
        mass += rp[i];
        diff += rp[i] * 0.5 * tv;
    }
    if (mass >= R->min_rel * R->root_mass[p]) {
        R->nodes++;
        if (diff / mass > R->worst) R->worst = diff / mass;
    }
    float *saved = (float *)malloc(sizeof(float) * NUM_COMBOS);
    for (int i = 0; i < NUM_COMBOS; i++) saved[i] = rp[i];
    for (int x = 0; x < k; x++) {
        ss_node_strategy(R->a, na, sa);
        for (int i = 0; i < NUM_COMBOS; i++) rp[i] = saved[i] * sa[x * NUM_COMBOS + i];
        reach_cmp_walk(R, ss_node_child(R->a, na, x), ss_node_child(R->b, nb, x), reach0, reach1);
    }
    for (int i = 0; i < NUM_COMBOS; i++) rp[i] = saved[i];
    free(saved);
}

static inline double reach_weighted_diff(const ss_solver *a, const ss_solver *b, const float *r0, const float *r1,
                                  double min_rel, int *nodes)
{
    ReachCmp R = { a, b, min_rel, 0.0, 0, { 0, 0 } };
    float *x0 = (float *)malloc(sizeof(float) * NUM_COMBOS), *x1 = (float *)malloc(sizeof(float) * NUM_COMBOS);
    for (int i = 0; i < NUM_COMBOS; i++) {
        x0[i] = r0[i]; x1[i] = r1[i];
        R.root_mass[0] += r0[i]; R.root_mass[1] += r1[i];
    }
    reach_cmp_walk(&R, ss_root(a), ss_root(b), x0, x1);
    free(x0);
    free(x1);
    *nodes = R.nodes;
    return R.worst;
}
