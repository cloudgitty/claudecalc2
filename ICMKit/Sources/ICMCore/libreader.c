#include "libreader.h"
#include "f16.h"
#include "flopclass.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Layout constants: see LIBRARY_FORMAT.md. */
#define HEADER_SIZE 64
#define INDEX_RECORD 8
#define VALUES_PER_ENTRY (2 * LR_VALUES_PER_SIDE)

static const double SPRS[3] = { 1.5, 3.5, 6.0 };

struct lr_library {
    int count;
    unsigned tiers;
    float thresholds[3], centres[4];
    lr_key *keys;
    float *values;   /* count * VALUES_PER_ENTRY, decoded */
};

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf32(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

static uint32_t crc32_bytes(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return c ^ 0xffffffffu;
}

static lr_library *fail(char *err, size_t len, const char *msg)
{
    if (err && len) snprintf(err, len, "%s", msg);
    return NULL;
}

lr_library *lr_open_memory(const void *data, size_t len, char *err, size_t err_len)
{
    const uint8_t *d = data;
    if (!d || len < HEADER_SIZE) return fail(err, err_len, "file too short");
    if (memcmp(d, "ICMLITE1", 8)) return fail(err, err_len, "not a Lite library (bad magic)");
    if (rd16(d + 8) != LR_FORMAT_VERSION) return fail(err, err_len, "unsupported format version");
    if (rd16(d + 10) != HEADER_SIZE || d[45] != VALUES_PER_ENTRY || d[46] != INDEX_RECORD)
        return fail(err, err_len, "unexpected layout");
    uint32_t count = rd32(d + 12), idx = rd32(d + 48), val = rd32(d + 52), size = rd32(d + 56);
    if (size != len || count > 1000000u || idx != HEADER_SIZE || val != idx + count * INDEX_RECORD ||
        (size_t)val + (size_t)count * VALUES_PER_ENTRY * 2 != len)
        return fail(err, err_len, "inconsistent sizes");
    if (crc32_bytes(d + HEADER_SIZE, len - HEADER_SIZE) != rd32(d + 60)) return fail(err, err_len, "CRC mismatch");

    lr_library *L = calloc(1, sizeof(*L));
    if (!L) return fail(err, err_len, "out of memory");
    L->count = (int)count;
    L->tiers = d[44];
    for (int i = 0; i < 3; i++) L->thresholds[i] = rdf32(d + 16 + 4 * i);
    for (int i = 0; i < 4; i++) L->centres[i] = rdf32(d + 28 + 4 * i);
    L->keys = calloc(count ? count : 1, sizeof(lr_key));
    L->values = calloc(count ? (size_t)count * VALUES_PER_ENTRY : 1, sizeof(float));
    if (!L->keys || !L->values) { lr_close(L); return fail(err, err_len, "out of memory"); }
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *k = d + idx + i * INDEX_RECORD;
        lr_key *K = &L->keys[i];
        K->hero_bucket = k[0]; K->villain_bucket = k[1]; K->position = k[2]; K->pot_type = k[3];
        K->spr_index = k[4]; K->flop_class = k[5]; K->flop_index = k[6]; K->tier = k[7];
        if (K->hero_bucket > 3 || K->villain_bucket > 3 || K->position > 1 || K->pot_type > 1 || K->spr_index > 2 ||
            K->flop_class >= FLOP_NUM_CLASSES || K->flop_index > 2) {
            lr_close(L);
            return fail(err, err_len, "bad index record");
        }
        for (int v = 0; v < VALUES_PER_ENTRY; v++)
            L->values[(size_t)i * VALUES_PER_ENTRY + v] = f16_to_float(rd16(d + val + (i * VALUES_PER_ENTRY + v) * 2));
    }
    return L;
}

void lr_close(lr_library *L)
{
    if (!L) return;
    free(L->keys);
    free(L->values);
    free(L);
}

int lr_entry_count(const lr_library *L) { return L ? L->count : 0; }
unsigned lr_tiers_complete(const lr_library *L) { return L ? L->tiers : 0; }

void lr_buckets(const lr_library *L, float thresholds[3], float centres[4])
{
    if (!L) return;
    if (thresholds) memcpy(thresholds, L->thresholds, sizeof(L->thresholds));
    if (centres) memcpy(centres, L->centres, sizeof(L->centres));
}

int lr_entry(const lr_library *L, int i, lr_key *key, float icm[LR_VALUES_PER_SIDE], float chip[LR_VALUES_PER_SIDE])
{
    if (!L || i < 0 || i >= L->count) return -1;
    if (key) *key = L->keys[i];
    const float *v = L->values + (size_t)i * VALUES_PER_ENTRY;
    if (icm) memcpy(icm, v, sizeof(float) * LR_VALUES_PER_SIDE);
    if (chip) memcpy(chip, v + LR_VALUES_PER_SIDE, sizeof(float) * LR_VALUES_PER_SIDE);
    return 0;
}

/* Two neighbouring grid points and weights for x on an increasing grid. */
static void bracket(double x, const double *grid, int n, int idx[2], double w[2])
{
    if (!(x > grid[0])) { idx[0] = idx[1] = 0; w[0] = 1; w[1] = 0; return; }
    if (x >= grid[n - 1]) { idx[0] = idx[1] = n - 1; w[0] = 1; w[1] = 0; return; }
    int i = 0;
    while (i + 1 < n - 1 && x >= grid[i + 1]) i++;
    double t = (x - grid[i]) / (grid[i + 1] - grid[i]);
    idx[0] = i; idx[1] = i + 1; w[0] = 1 - t; w[1] = t;
}

typedef struct { int n; double num[LR_VALUES_PER_SIDE]; int cnt[LR_VALUES_PER_SIDE]; int first; } Cell;

/* Averages the solved flops of one class at one grid corner. */
static void gather(const lr_library *L, int hb, int vb, int pos, int pot, int si, int cls, int icm, Cell *c)
{
    memset(c, 0, sizeof(*c));
    c->first = -1;
    int best_k = 99;
    for (int i = 0; i < L->count; i++) {
        const lr_key *K = &L->keys[i];
        if (K->hero_bucket != hb || K->villain_bucket != vb || K->position != pos || K->pot_type != pot ||
            K->spr_index != si || K->flop_class != cls)
            continue;
        const float *v = L->values + (size_t)i * VALUES_PER_ENTRY + (icm ? 0 : LR_VALUES_PER_SIDE);
        if (isnan(v[LR_V_BET_PCT])) continue;   /* no data for this side */
        c->n++;
        for (int j = 0; j < LR_VALUES_PER_SIDE; j++)
            if (!isnan(v[j])) { c->num[j] += v[j]; c->cnt[j]++; }
        if (K->flop_index < best_k) { best_k = K->flop_index; c->first = i; }
    }
}

static void fill_from(const lr_library *L, int entry, int icm, lr_values *out)
{
    const float *v = L->values + (size_t)entry * VALUES_PER_ENTRY + (icm ? 0 : LR_VALUES_PER_SIDE);
    out->bet_size = isnan(v[LR_V_BET_SIZE]) ? -1 : (int)lrintf(v[LR_V_BET_SIZE]);
    out->stack_off_category = isnan(v[LR_V_STACKOFF]) ? -1 : (int)lrintf(v[LR_V_STACKOFF]);
}

int lr_query(const lr_library *L, double hero_premium, double villain_premium, int position,
             int pot_type, double spr, const uint8_t flop[3], int icm, lr_values *out)
{
    if (!L || !out || L->count == 0) return -1;
    int cls = flop_classify(flop);
    if (cls < 0 || position < 0 || position > 1 || pot_type < 0 || pot_type > 1) return -1;
    memset(out, 0, sizeof(*out));
    double centres[4];
    for (int i = 0; i < 4; i++) centres[i] = L->centres[i];
    int hi[2], vi[2], si[2];
    double hw[2], vw[2], sw[2];
    bracket(hero_premium, centres, 4, hi, hw);
    bracket(villain_premium, centres, 4, vi, vw);
    bracket(spr, SPRS, 3, si, sw);

    double acc[LR_VALUES_PER_SIDE] = { 0 }, wsum[LR_VALUES_PER_SIDE] = { 0 }, best_w = -1;
    int best_entry = -1, any = 0;
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 2; b++)
            for (int s = 0; s < 2; s++) {
                double w = hw[a] * vw[b] * sw[s];
                if (w <= 0) continue;
                Cell c;
                gather(L, hi[a], vi[b], position, pot_type, si[s], cls, icm, &c);
                if (!c.n) continue;
                any = 1;
                for (int j = 0; j < LR_VALUES_PER_SIDE; j++)
                    if (c.cnt[j]) { acc[j] += w * c.num[j] / c.cnt[j]; wsum[j] += w; }
                if (w > best_w) { best_w = w; best_entry = c.first; }
            }
    if (any) {
        out->source = LR_SOURCE_INTERPOLATED;
        out->bet_pct = (float)(acc[LR_V_BET_PCT] / wsum[LR_V_BET_PCT]);
        out->bluff_pct = wsum[LR_V_BLUFF_PCT] > 0 ? (float)(acc[LR_V_BLUFF_PCT] / wsum[LR_V_BLUFF_PCT]) : 0.0f;
        out->check_raise_pct = wsum[LR_V_CHECK_RAISE_PCT] > 0 ? (float)(acc[LR_V_CHECK_RAISE_PCT] / wsum[LR_V_CHECK_RAISE_PCT]) : -1.0f;
        fill_from(L, best_entry, icm, out);
        return 0;
    }

    /* Fallback: nearest solved spot. Distance in grid steps; a different flop class,
       position or pot type costs more than any bucket or SPR step. */
    int th = hw[1] > hw[0] ? hi[1] : hi[0], tv = vw[1] > vw[0] ? vi[1] : vi[0], ts = sw[1] > sw[0] ? si[1] : si[0];
    int best = -1;
    double bd = 1e9;
    for (int i = 0; i < L->count; i++) {
        const lr_key *K = &L->keys[i];
        const float *v = L->values + (size_t)i * VALUES_PER_ENTRY + (icm ? 0 : LR_VALUES_PER_SIDE);
        if (isnan(v[LR_V_BET_PCT])) continue;
        double d = abs(K->hero_bucket - th) + abs(K->villain_bucket - tv) + abs(K->spr_index - ts) +
                   (K->flop_class != cls ? 4 : 0) + (K->position != position ? 20 : 0) + (K->pot_type != pot_type ? 10 : 0) +
                   0.01 * K->flop_index;
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return -1;
    const float *v = L->values + (size_t)best * VALUES_PER_ENTRY + (icm ? 0 : LR_VALUES_PER_SIDE);
    out->source = LR_SOURCE_NEAREST;
    out->bet_pct = v[LR_V_BET_PCT];
    out->bluff_pct = isnan(v[LR_V_BLUFF_PCT]) ? 0.0f : v[LR_V_BLUFF_PCT];
    out->check_raise_pct = isnan(v[LR_V_CHECK_RAISE_PCT]) ? -1.0f : v[LR_V_CHECK_RAISE_PCT];
    fill_from(L, best, icm, out);
    return 0;
}
