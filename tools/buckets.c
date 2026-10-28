/*
 * Risk-premium matchup buckets (spec "Matchups by risk premium").
 * Computes every ordered pair at both canonical tables, sets the bucket
 * thresholds at the quartiles of all hero premiums, picks a real pair for each
 * of the 16 (hero bucket, villain bucket) cells and writes config/buckets.json.
 *   usage: buckets [output.json]
 */
#include "icmcore.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define NT 2
#define NP 6
#define NPAIRS (NT * NP * (NP - 1))

static const char *TABLE_NAME[NT] = { "runaway_chip_leader", "chip_leader" };
static const double STACKS[NT][NP] = {
    { 100, 40, 30, 25, 15, 10 },
    { 60, 50, 35, 30, 15, 10 },
};
static const double PAYOUTS[NP] = { 40, 25, 15, 10, 6, 4 };

typedef struct {
    int table, hero, villain;
    double rp_hero, rp_villain;
    int b_hero, b_villain;
} Pair;

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* Linear-interpolation quantile of sorted values (same as numpy's default). */
static double quantile(const double *v, int n, double q)
{
    double pos = q * (n - 1);
    int i = (int)floor(pos);
    if (i >= n - 1) return v[n - 1];
    return v[i] + (pos - i) * (v[i + 1] - v[i]);
}

static int bucket_of(double x, const double *thr)
{
    int b = 0;
    while (b < 3 && x >= thr[b]) b++;
    return b;
}

int main(int argc, char **argv)
{
    const char *out_path = argc > 1 ? argv[1] : "config/buckets.json";
    Pair pairs[NPAIRS];
    double values[NPAIRS];
    int np = 0;

    for (int t = 0; t < NT; t++)
        for (int h = 0; h < NP; h++)
            for (int v = 0; v < NP; v++) {
                if (h == v) continue;
                Pair *p = &pairs[np];
                p->table = t; p->hero = h; p->villain = v;
                if (risk_premium(NP, STACKS[t], NP, PAYOUTS, h, v, &p->rp_hero) ||
                    risk_premium(NP, STACKS[t], NP, PAYOUTS, v, h, &p->rp_villain)) {
                    fprintf(stderr, "risk_premium failed\n");
                    return 1;
                }
                values[np] = p->rp_hero;
                np++;
            }

    double sorted[NPAIRS];
    for (int i = 0; i < np; i++) sorted[i] = values[i];
    qsort(sorted, (size_t)np, sizeof(double), cmp_double);
    double thr[3] = { quantile(sorted, np, 0.25), quantile(sorted, np, 0.5), quantile(sorted, np, 0.75) };
    double edges[5] = { sorted[0], thr[0], thr[1], thr[2], sorted[np - 1] };
    double centre[4];
    for (int b = 0; b < 4; b++) centre[b] = 0.5 * (edges[b] + edges[b + 1]);

    for (int i = 0; i < np; i++) {
        pairs[i].b_hero = bucket_of(pairs[i].rp_hero, thr);
        pairs[i].b_villain = bucket_of(pairs[i].rp_villain, thr);
    }

    FILE *f = fopen(out_path, "w");
    if (!f) { perror(out_path); return 1; }
    fprintf(f, "{\n  \"format\": 1,\n");
    fprintf(f, "  \"description\": \"Risk premium = ICM break-even equity minus 0.5 for calling an all-in of the effective stack with no other money in the pot (fractions, 0.05 = 5 points). Buckets split all 60 hero premiums at the quartiles.\",\n");
    fprintf(f, "  \"payouts_pct\": [40, 25, 15, 10, 6, 4],\n");
    fprintf(f, "  \"tables\": [\n");
    for (int t = 0; t < NT; t++) {
        fprintf(f, "    {\"name\": \"%s\", \"stacks_bb\": [", TABLE_NAME[t]);
        for (int i = 0; i < NP; i++) fprintf(f, "%s%g", i ? ", " : "", STACKS[t][i]);
        fprintf(f, "], \"roles\": [");
        for (int i = 0; i < NP; i++)
            fprintf(f, "%s\"%s\"", i ? ", " : "", stack_role_name(stack_role(NP, STACKS[t], i, 1.0)));
        fprintf(f, "]}%s\n", t + 1 < NT ? "," : "");
    }
    fprintf(f, "  ],\n");
    fprintf(f, "  \"thresholds\": [%.6f, %.6f, %.6f],\n", thr[0], thr[1], thr[2]);
    fprintf(f, "  \"bucket_rule\": \"bucket 0: rp < t0; 1: t0 <= rp < t1; 2: t1 <= rp < t2; 3: rp >= t2\",\n");
    fprintf(f, "  \"bucket_edges\": [%.6f, %.6f, %.6f, %.6f, %.6f],\n", edges[0], edges[1], edges[2], edges[3], edges[4]);
    fprintf(f, "  \"bucket_centres\": [%.6f, %.6f, %.6f, %.6f],\n", centre[0], centre[1], centre[2], centre[3]);

    fprintf(f, "  \"cells\": [\n");
    int skipped = 0;
    printf("cell  hero  villain  pair\n");
    for (int a = 0; a < 4; a++)
        for (int b = 0; b < 4; b++) {
            int best = -1;
            double bd = 1e300;
            for (int i = 0; i < np; i++) {
                if (pairs[i].b_hero != a || pairs[i].b_villain != b) continue;
                double dx = pairs[i].rp_hero - centre[a], dy = pairs[i].rp_villain - centre[b];
                double d = dx * dx + dy * dy;
                if (d < bd) { bd = d; best = i; }
            }
            int last = a == 3 && b == 3;
            if (best < 0) {
                skipped++;
                fprintf(f, "    {\"hero_bucket\": %d, \"villain_bucket\": %d, \"skipped\": true}%s\n", a, b, last ? "" : ",");
                printf("%d,%d   skipped (no real pair)\n", a, b);
                continue;
            }
            const Pair *p = &pairs[best];
            const double *s = STACKS[p->table];
            int count = 0;
            for (int i = 0; i < np; i++) count += pairs[i].b_hero == a && pairs[i].b_villain == b;
            fprintf(f, "    {\"hero_bucket\": %d, \"villain_bucket\": %d, \"skipped\": false, \"table\": \"%s\", "
                       "\"hero_seat\": %d, \"villain_seat\": %d, \"hero_stack_bb\": %g, \"villain_stack_bb\": %g, "
                       "\"hero_role\": \"%s\", \"villain_role\": \"%s\", \"hero_premium\": %.6f, \"villain_premium\": %.6f, "
                       "\"candidates\": %d}%s\n",
                    a, b, TABLE_NAME[p->table], p->hero, p->villain, s[p->hero], s[p->villain],
                    stack_role_name(stack_role(NP, s, p->hero, 1.0)), stack_role_name(stack_role(NP, s, p->villain, 1.0)),
                    p->rp_hero, p->rp_villain, count, last ? "" : ",");
            printf("%d,%d   %5.1f%%  %5.1f%%   %s %g vs %g  (%d candidates)\n", a, b, 100 * p->rp_hero, 100 * p->rp_villain,
                   TABLE_NAME[p->table], s[p->hero], s[p->villain], count);
        }
    fprintf(f, "  ],\n  \"skipped_cells\": %d,\n", skipped);

    fprintf(f, "  \"pairs\": [\n");
    for (int i = 0; i < np; i++) {
        const Pair *p = &pairs[i];
        fprintf(f, "    {\"table\": \"%s\", \"hero_seat\": %d, \"villain_seat\": %d, \"hero_stack_bb\": %g, \"villain_stack_bb\": %g, "
                   "\"hero_premium\": %.6f, \"villain_premium\": %.6f, \"hero_bucket\": %d, \"villain_bucket\": %d}%s\n",
                TABLE_NAME[p->table], p->hero, p->villain, STACKS[p->table][p->hero], STACKS[p->table][p->villain],
                p->rp_hero, p->rp_villain, p->b_hero, p->b_villain, i + 1 < np ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);

    printf("thresholds (quartiles of %d hero premiums): %.2f%% %.2f%% %.2f%%; skipped cells: %d\n",
           np, 100 * thr[0], 100 * thr[1], 100 * thr[2], skipped);
    return 0;
}
