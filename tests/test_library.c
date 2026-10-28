/*
 * Library packer and reader (libreader) on synthetic results with known values:
 * round trip within 16-bit precision (the mechanics of spec test 6), exact lookups,
 * interpolation between premium buckets and SPRs, fallback to the nearest solved
 * spot, and rejection of a corrupted file. Run from the repo root (reads
 * config/buckets.json); writes into a temporary directory.
 */
#include "lb.h"
#include "check.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char dir[256];

/* Known values for a player of a solve: depend on solve index and side. */
static void synthetic(int solve, int side, double v[5])
{
    v[0] = 10.0 + (solve % 37) * 2.0 + side * 0.5;          /* bet_pct */
    v[1] = (solve + side) % 3;                               /* bet_size */
    v[2] = side == 0 ? 5.0 + (solve % 11) : NAN;             /* check_raise_pct */
    v[3] = (solve * 7 + side) % 6;                           /* stackoff */
    v[4] = 1.0 / 3.0 + (solve % 13);                         /* bluff_pct (not exact in f16) */
}

static void write_result(const LbManifest *m, int i)
{
    char path[512], buf[2048];
    size_t o = 0;
    o += (size_t)snprintf(buf + o, sizeof buf - o, "{\"id\": \"%s\", \"players\": [", m->solves[i].id);
    for (int side = 0; side < 2; side++) {
        double v[5];
        synthetic(i, side, v);
        char cr[32];
        if (isnan(v[2])) snprintf(cr, sizeof cr, "null"); else snprintf(cr, sizeof cr, "%.9g", v[2]);
        o += (size_t)snprintf(buf + o, sizeof buf - o,
                              "%s{\"bet_pct\": %.9g, \"bet_size\": %d, \"check_raise_pct\": %s, \"stackoff_category\": %d, "
                              "\"bluff_pct\": %.9g, \"category_bet_pct\": [1, 2, 3, 4, 5, 6]}",
                              side ? ", " : "", v[0], (int)v[1], cr, (int)v[3], v[4]);
    }
    o += (size_t)snprintf(buf + o, sizeof buf - o, "]}\n");
    snprintf(path, sizeof path, "%s/%s.json.gz", dir, m->solves[i].id);
    CHECK(lb_write_gz_atomic(path, buf, o) == 0, "write %s", path);
}

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)len);
    if (d && fread(d, 1, (size_t)len, f) != (size_t)len) { free(d); d = NULL; }
    fclose(f);
    *n = (size_t)len;
    return d;
}

int main(void)
{
    snprintf(dir, sizeof dir, "/tmp/icmkit_test_library_%ld", (long)getpid());
    LbBuckets b;
    LbManifest *m = calloc(1, sizeof(LbManifest));
    CHECK(m && lb_load_buckets("config/buckets.json", &b) == 0, "load buckets");
    CHECK(lb_make_manifest(&b, m) == 0 && m->nsolves == 1836 && m->nentries == 3024, "manifest sizes %d / %d",
          m->nsolves, m->nentries);
    lb_mkdirs(dir);

    /* Solve every T1 spot except the single-raised paired flop, and the 3-bet T2
       spots on dry-high flops, so interpolation and fallback both get exercised. */
    int solved = 0;
    for (int i = 0; i < m->nsolves; i++) {
        const LbSolve *s = &m->solves[i];
        int t1 = s->tier == 1 && !(s->pot_type == LB_SRP && s->flop_class == FLOP_PAIRED);
        int t2 = s->tier == 2 && s->pot_type == LB_3BP && s->flop_class == FLOP_DRY_HIGH;
        if (t1 || t2) { write_result(m, i); solved++; }
    }

    char bin[512], json[512];
    snprintf(bin, sizeof bin, "%s/LiteLibrary.bin", dir);
    snprintf(json, sizeof json, "%s/LiteLibrary.json", dir);
    int packed = 0;
    size_t size = 0;
    CHECK(lb_pack(m, &b, dir, bin, json, &packed, &size) == 0, "pack");
    printf("synthetic library: %d solves, %d entries packed, %zu bytes\n", solved, packed, size);

    /* round trip */
    int checked = 0;
    int failures = lb_check_pack(m, dir, bin, &checked, 1);
    CHECK(failures == 0 && checked == packed * 10, "round trip: %d failures over %d values", failures, checked);

    size_t n = 0;
    uint8_t *data = slurp(bin, &n);
    char err[200];
    lr_library *L = lr_open_memory(data, n, err, sizeof err);
    CHECK(L != NULL, "open: %s", err);
    if (!L) return check_finish("test_library");
    CHECK(lr_entry_count(L) == packed, "entry count");
    CHECK(lr_tiers_complete(L) == 0, "no tier is complete");
    float thr[3], cen[4];
    lr_buckets(L, thr, cen);
    CHECK(fabsf(thr[0] - (float)b.thresholds[0]) < 1e-6f && fabsf(cen[3] - (float)b.centres[3]) < 1e-6f, "bucket header");

    /* exact query: at a bucket centre, a solved SPR and a representative flop the
       value equals the stored one (dry-high class has flop 1 only in T1) */
    {
        const LbEntry *e = NULL;
        for (int i = 0; i < m->nentries && !e; i++) {
            const LbEntry *x = &m->entries[i];
            if (x->tier == 1 && x->pot_type == LB_3BP && x->flop_class == FLOP_DRY_HIGH && x->position == LB_HERO_OOP) e = x;
        }
        double v[5];
        synthetic(e->solve, e->hero_player, v);
        uint8_t flop[3];
        cards_parse("Kd8s3h", flop, 3);   /* another dry-high flop */
        lr_values out;
        CHECK(lr_query(L, b.centres[e->hero_bucket], b.centres[e->villain_bucket], LR_OUT_OF_POSITION,
                       LR_POT_THREE_BET, 1.5, flop, 1, &out) == 0, "exact query");
        CHECK(out.source == LR_SOURCE_INTERPOLATED, "exact query source");
        CHECK(fabs(out.bet_pct - v[0]) < 0.02, "exact bet_pct %.3f vs %.3f", out.bet_pct, v[0]);
        CHECK(out.bet_size == (int)v[1] && out.stack_off_category == (int)v[3], "exact categorical values");
        CHECK(fabs(out.check_raise_pct - v[2]) < 0.02, "exact check-raise %.3f vs %.3f", out.check_raise_pct, v[2]);
        lr_values chip;
        lr_query(L, b.centres[e->hero_bucket], b.centres[e->villain_bucket], LR_OUT_OF_POSITION, LR_POT_THREE_BET, 1.5,
                 flop, 0, &chip);
        double cv[5];
        synthetic(e->chip_solve, e->hero_player, cv);
        CHECK(fabs(chip.bet_pct - cv[0]) < 0.02, "chip-EV baseline %.3f vs %.3f", chip.bet_pct, cv[0]);

        /* halfway between SPR 1.5 and 3.5 on a solved 3-bet dry-high cell: mean of both */
        const LbEntry *e2 = NULL;
        for (int i = 0; i < m->nentries && !e2; i++) {
            const LbEntry *x = &m->entries[i];
            if (x->tier == 2 && x->pot_type == e->pot_type && x->flop_class == e->flop_class && x->position == e->position &&
                x->hero_bucket == e->hero_bucket && x->villain_bucket == e->villain_bucket)
                e2 = x;
        }
        double v2[5];
        synthetic(e2->solve, e2->hero_player, v2);
        CHECK(lr_query(L, b.centres[e->hero_bucket], b.centres[e->villain_bucket], LR_OUT_OF_POSITION,
                       LR_POT_THREE_BET, 2.5, flop, 1, &out) == 0, "SPR interpolation query");
        CHECK(fabs(out.bet_pct - 0.5 * (v[0] + v2[0])) < 0.03, "SPR interpolation %.3f vs %.3f", out.bet_pct,
              0.5 * (v[0] + v2[0]));
    }

    /* premium interpolation: halfway between two bucket centres = mean of the cells */
    {
        int hb = 1, vb = 1, hb2 = 2;
        double want = 0;
        int have = 0;
        for (int i = 0; i < m->nentries; i++) {
            const LbEntry *x = &m->entries[i];
            if (x->tier == 1 && x->pot_type == LB_3BP && x->flop_class == FLOP_MONOTONE && x->position == LB_HERO_IP &&
                x->villain_bucket == vb && (x->hero_bucket == hb || x->hero_bucket == hb2)) {
                double v[5];
                synthetic(x->solve, x->hero_player, v);
                want += 0.5 * v[0];
                have++;
            }
        }
        uint8_t flop[3];
        cards_parse("Qc8c2c", flop, 3);
        lr_values out;
        lr_query(L, 0.5 * (b.centres[hb] + b.centres[hb2]), b.centres[vb], LR_IN_POSITION, LR_POT_THREE_BET, 1.5, flop, 1, &out);
        CHECK(have == 2 && fabs(out.bet_pct - want) < 0.03, "premium interpolation %.3f vs %.3f", out.bet_pct, want);
        CHECK(out.check_raise_pct < 0, "no check-raise in position");
    }

    /* fallback: single-raised paired flops are not solved at all -> nearest spot */
    {
        uint8_t flop[3];
        cards_parse("QsQd7c", flop, 3);
        lr_values out;
        CHECK(lr_query(L, b.centres[1], b.centres[2], LR_IN_POSITION, LR_POT_SINGLE_RAISED, 1.5, flop, 1, &out) == 0,
              "fallback query");
        CHECK(out.source == LR_SOURCE_NEAREST, "fallback source %d", out.source);
        CHECK(out.bet_pct > 0, "fallback has a value");
        /* skipped cell (0,0): interpolation uses neighbours or falls back, never fails */
        CHECK(lr_query(L, 0.0, 0.0, LR_OUT_OF_POSITION, LR_POT_SINGLE_RAISED, 6.0, flop, 0, &out) == 0, "corner query");
    }

    /* corrupted file is rejected */
    data[n - 1] ^= 0x40;
    lr_library *bad = lr_open_memory(data, n, err, sizeof err);
    CHECK(bad == NULL && strstr(err, "CRC"), "corruption not detected (%s)", err);
    data[n - 1] ^= 0x40;
    CHECK(lr_open_memory(data, 10, err, sizeof err) == NULL, "short file accepted");

    lr_close(L);
    free(data);
    free(m);
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
    return check_finish("test_library");
}
