/* Library builder: packing results into LiteLibrary.bin / .json, and test 6. */
#include "lb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define HEADER_SIZE 64
#define INDEX_RECORD 8
#define VALUES_PER_ENTRY 10

static const char *SPR_FILE[LB_NUM_SPR] = { "1.5", "3.5", "6" };

static void w16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v & 255); p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void wf32(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); w32(p, u); }

/* The five stored values of players[hero] in a result file, in file order. */
static int side_values(const char *json, int hero, float out[5])
{
    const char *os, *oe;
    for (int i = 0; i < 5; i++) out[i] = NAN;
    if (!json || lb_json_player(json, hero, &os, &oe)) return -1;
    static const char *keys[5] = { "bet_pct", "bet_size", "check_raise_pct", "stackoff_category", "bluff_pct" };
    for (int i = 0; i < 5; i++) {
        double x;
        if (lb_json_number(os, oe, keys[i], &x)) return -1;
        out[i] = (float)x;
    }
    if (out[LR_V_BET_SIZE] < 0) out[LR_V_BET_SIZE] = -1;
    return 0;
}

typedef struct { char *json; int loaded; } Cache;

static const char *result_json(Cache *cache, const LbManifest *m, int solve, const char *dir)
{
    if (!cache[solve].loaded) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.json.gz", dir, m->solves[solve].id);
        cache[solve].json = lb_file_exists(path) ? lb_read_gz(path) : NULL;
        cache[solve].loaded = 1;
    }
    return cache[solve].json;
}

int lb_pack(const LbManifest *m, const LbBuckets *b, const char *results_dir, const char *bin_path,
            const char *json_path, int *packed_entries, size_t *bin_size)
{
    Cache *cache = calloc((size_t)m->nsolves, sizeof(Cache));
    int *use = calloc((size_t)m->nentries, sizeof(int));
    float *vals = calloc((size_t)m->nentries * VALUES_PER_ENTRY, sizeof(float));
    if (!cache || !use || !vals) { free(cache); free(use); free(vals); return -1; }

    int count = 0, tier_total[6] = { 0 }, tier_done[6] = { 0 };
    for (int i = 0; i < m->nentries; i++) {
        const LbEntry *e = &m->entries[i];
        tier_total[e->tier]++;
        const char *ij = result_json(cache, m, e->solve, results_dir);
        if (!ij) continue;
        float *v = vals + (size_t)i * VALUES_PER_ENTRY;
        if (side_values(ij, e->hero_player, v)) continue;
        const char *cj = result_json(cache, m, e->chip_solve, results_dir);
        if (!cj || side_values(cj, e->hero_player, v + 5)) for (int k = 5; k < 10; k++) v[k] = NAN;
        if (e->position == LB_HERO_IP) { v[LR_V_CHECK_RAISE_PCT] = NAN; v[5 + LR_V_CHECK_RAISE_PCT] = NAN; }
        use[i] = 1;
        count++;
        tier_done[e->tier]++;
    }
    unsigned tiers = 0;
    for (int t = 1; t <= 5; t++) if (tier_total[t] && tier_done[t] == tier_total[t]) tiers |= 1u << (t - 1);

    size_t size = HEADER_SIZE + (size_t)count * INDEX_RECORD + (size_t)count * VALUES_PER_ENTRY * 2;
    uint8_t *buf = calloc(size, 1);
    if (!buf) { free(cache); free(use); free(vals); return -1; }
    memcpy(buf, "ICMLITE1", 8);
    w16(buf + 8, LR_FORMAT_VERSION);
    w16(buf + 10, HEADER_SIZE);
    w32(buf + 12, (uint32_t)count);
    for (int i = 0; i < 3; i++) wf32(buf + 16 + 4 * i, (float)b->thresholds[i]);
    for (int i = 0; i < 4; i++) wf32(buf + 28 + 4 * i, (float)b->centres[i]);
    buf[44] = (uint8_t)tiers;
    buf[45] = VALUES_PER_ENTRY;
    buf[46] = INDEX_RECORD;
    uint32_t idx = HEADER_SIZE, val = HEADER_SIZE + (uint32_t)count * INDEX_RECORD;
    w32(buf + 48, idx);
    w32(buf + 52, val);
    w32(buf + 56, (uint32_t)size);
    int j = 0;
    for (int i = 0; i < m->nentries; i++) {
        if (!use[i]) continue;
        const LbEntry *e = &m->entries[i];
        uint8_t *k = buf + idx + (size_t)j * INDEX_RECORD;
        k[0] = (uint8_t)e->hero_bucket; k[1] = (uint8_t)e->villain_bucket; k[2] = (uint8_t)e->position;
        k[3] = (uint8_t)e->pot_type; k[4] = (uint8_t)e->spr_idx; k[5] = (uint8_t)e->flop_class;
        k[6] = (uint8_t)e->flop_k; k[7] = (uint8_t)e->tier;
        for (int v = 0; v < VALUES_PER_ENTRY; v++)
            w16(buf + val + ((size_t)j * VALUES_PER_ENTRY + v) * 2, f16_from_float(vals[(size_t)i * VALUES_PER_ENTRY + v]));
        j++;
    }
    w32(buf + 60, (uint32_t)crc32(0L, buf + HEADER_SIZE, (uInt)(size - HEADER_SIZE)));

    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp", bin_path);
    FILE *f = fopen(tmp, "wb");
    int ok = f && fwrite(buf, 1, size, f) == size;
    if (f && fclose(f) != 0) ok = 0;
    if (!ok || rename(tmp, bin_path)) ok = 0;
    free(buf);

    if (ok && json_path) {
        snprintf(tmp, sizeof tmp, "%s.tmp", json_path);
        f = fopen(tmp, "w");
        if (!f) ok = 0;
        else {
            fprintf(f, "{\n  \"format_version\": %d,\n  \"entries\": %d,\n  \"tiers_complete\": [", LR_FORMAT_VERSION, count);
            int first = 1;
            for (int t = 1; t <= 5; t++) if (tiers & (1u << (t - 1))) { fprintf(f, "%s\"T%d\"", first ? "" : ", ", t); first = 0; }
            fprintf(f, "],\n  \"thresholds\": [%.6f, %.6f, %.6f],\n  \"bucket_centres\": [%.6f, %.6f, %.6f, %.6f],\n",
                    b->thresholds[0], b->thresholds[1], b->thresholds[2], b->centres[0], b->centres[1], b->centres[2], b->centres[3]);
            fprintf(f, "  \"value_order\": [\"bet_pct\", \"bet_size (0=33%%,1=75%%,2=all-in,-1=never)\", \"check_raise_pct\", "
                       "\"stackoff_category (0=two pair+,1=top pair,2=mid/bottom pair,3=strong draw,4=weak draw,5=air,-1=none)\", \"bluff_pct\"],\n");
            fprintf(f, "  \"spots\": [\n");
            int n = 0;
            for (int i = 0; i < m->nentries; i++) {
                if (!use[i]) continue;
                const LbEntry *e = &m->entries[i];
                const float *v = vals + (size_t)i * VALUES_PER_ENTRY;
                fprintf(f, "    {\"tier\": %d, \"hero_bucket\": %d, \"villain_bucket\": %d, \"position\": \"%s\", \"pot_type\": \"%s\", "
                           "\"spr\": %s, \"flop_class\": \"%s\", \"flop\": \"%s\", \"solve\": \"%s\", \"icm\": [",
                        e->tier, e->hero_bucket, e->villain_bucket, e->position == LB_HERO_IP ? "ip" : "oop",
                        LB_POT_NAME[e->pot_type], SPR_FILE[e->spr_idx], flop_class_name(e->flop_class),
                        flop_representative(e->flop_class, e->flop_k), m->solves[e->solve].id);
                for (int k = 0; k < 10; k++) {
                    if (k == 5) fprintf(f, "], \"chip_ev\": [");
                    else if (k) fprintf(f, ", ");
                    if (isnan(v[k])) fprintf(f, "null"); else fprintf(f, "%.4g", v[k]);
                }
                fprintf(f, "]}%s\n", ++n < count ? "," : "");
            }
            fprintf(f, "  ]\n}\n");
            if (fclose(f) != 0 || rename(tmp, json_path)) ok = 0;
        }
    }
    for (int i = 0; i < m->nsolves; i++) free(cache[i].json);
    free(cache);
    free(use);
    free(vals);
    if (packed_entries) *packed_entries = count;
    if (bin_size) *bin_size = size;
    return ok ? 0 : -1;
}

/* Test 6: every value read back from the bin matches its source result within
   16-bit float precision. */
int lb_check_pack(const LbManifest *m, const char *results_dir, const char *bin_path, int *checked, int verbose)
{
    FILE *f = fopen(bin_path, "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", bin_path); return -1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n);
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(data); return -1; }
    fclose(f);
    char err[200];
    lr_library *L = lr_open_memory(data, (size_t)n, err, sizeof err);
    free(data);
    if (!L) { fprintf(stderr, "reader rejected %s: %s\n", bin_path, err); return -1; }

    Cache *cache = calloc((size_t)m->nsolves, sizeof(Cache));
    int failures = 0, values = 0, entries = lr_entry_count(L);
    for (int i = 0; i < entries; i++) {
        lr_key k;
        float rv[10];
        lr_entry(L, i, &k, rv, rv + 5);
        int found = -1;
        for (int e = 0; e < m->nentries && found < 0; e++) {
            const LbEntry *E = &m->entries[e];
            if (E->hero_bucket == k.hero_bucket && E->villain_bucket == k.villain_bucket && E->position == k.position &&
                E->pot_type == k.pot_type && E->spr_idx == k.spr_index && E->flop_class == k.flop_class && E->flop_k == k.flop_index)
                found = e;
        }
        if (found < 0) { failures++; if (verbose) fprintf(stderr, "entry %d not in the manifest\n", i); continue; }
        const LbEntry *E = &m->entries[found];
        float src[10];
        side_values(result_json(cache, m, E->solve, results_dir), E->hero_player, src);
        if (side_values(result_json(cache, m, E->chip_solve, results_dir), E->hero_player, src + 5))
            for (int v = 5; v < 10; v++) src[v] = NAN;
        if (E->position == LB_HERO_IP) { src[LR_V_CHECK_RAISE_PCT] = NAN; src[5 + LR_V_CHECK_RAISE_PCT] = NAN; }
        for (int v = 0; v < 10; v++) {
            values++;
            if (isnan(src[v]) && isnan(rv[v])) continue;
            /* binary16: 11 significant bits -> relative error <= 2^-11, plus subnormal floor */
            double tol = fabs(src[v]) * (1.0 / 2048.0) + 6.0e-8;
            if (isnan(src[v]) != isnan(rv[v]) || fabs(rv[v] - src[v]) > tol) {
                failures++;
                if (verbose) fprintf(stderr, "%s value %d: source %.6g, read %.6g\n", m->solves[E->solve].id, v, src[v], rv[v]);
            }
        }
    }
    for (int i = 0; i < m->nsolves; i++) free(cache[i].json);
    free(cache);
    lr_close(L);
    if (checked) *checked = values;
    return failures;
}
