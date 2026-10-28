/* Library builder: buckets, manifest, spot configuration, file and JSON helpers. */
#include "lb.h"
#include "tool_common.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

const double LB_SPR[LB_NUM_SPR] = { 1.5, 3.5, 6.0 };
const double LB_POT[2] = { 6.0, 15.0 };
const char *LB_POT_NAME[2] = { "srp", "3bp" };

static const char *SPR_TAG[LB_NUM_SPR] = { "15", "35", "60" };
static const char *SPR_FILE[LB_NUM_SPR] = { "1.5", "3.5", "6" };

/* ------------------------------------------------------------------------ */
/* JSON helpers (only for the files written by these tools)                 */

const char *lb_json_find(const char *start, const char *end, const char *key)
{
    char pat[96];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    size_t n = strlen(pat);
    for (const char *p = start; p && p + n <= end; p++) {
        p = strstr(p, pat);
        if (!p || p + n > end) return NULL;
        const char *v = p + n;
        while (*v == ' ') v++;
        return v;
    }
    return NULL;
}

int lb_json_number(const char *start, const char *end, const char *key, double *out)
{
    const char *v = lb_json_find(start, end, key);
    if (!v) return -1;
    if (!strncmp(v, "null", 4)) { *out = NAN; return 0; }
    char *e;
    double x = strtod(v, &e);
    if (e == v) return -1;
    *out = x;
    return 0;
}

static int parse_numbers(const char *p, double *out, int max)
{
    /* p points at '[' */
    if (!p || *p != '[') return -1;
    p++;
    int n = 0;
    while (*p && *p != ']' && n < max) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']') break;
        char *e;
        out[n] = strtod(p, &e);
        if (e == p) return -1;
        n++;
        p = e;
    }
    return n;
}

int lb_json_player(const char *json, int i, const char **os, const char **oe)
{
    const char *p = strstr(json, "\"players\": [");
    if (!p) return -1;
    p += strlen("\"players\": [");
    for (int k = 0; k <= i; k++) {
        p = strchr(p, '{');
        if (!p) return -1;
        const char *q = strchr(p, '}');
        if (!q) return -1;
        if (k == i) { *os = p; *oe = q; return 0; }
        p = q + 1;
    }
    return -1;
}

/* ------------------------------------------------------------------------ */
/* buckets.json                                                             */

int lb_load_buckets(const char *path, LbBuckets *b)
{
    char *txt = read_file(path);
    if (!txt) { fprintf(stderr, "cannot read %s\n", path); return -1; }
    memset(b, 0, sizeof(*b));
    const char *end = txt + strlen(txt);
    int ok = 1;
    double tmp[8];

    const char *p = lb_json_find(txt, end, "payouts_pct");
    ok &= parse_numbers(p, b->payouts, LB_TABLE_SIZE) == LB_TABLE_SIZE;
    p = lb_json_find(txt, end, "thresholds");
    ok &= parse_numbers(p, b->thresholds, 3) == 3;
    p = lb_json_find(txt, end, "bucket_centres");
    ok &= parse_numbers(p, b->centres, 4) == 4;

    const char *t = lb_json_find(txt, end, "tables");
    for (int k = 0; ok && k < LB_NUM_TABLES; k++) {
        t = t ? strstr(t, "{\"name\": \"") : NULL;
        if (!t) { ok = 0; break; }
        t += strlen("{\"name\": \"");
        const char *q = strchr(t, '"');
        size_t len = (size_t)(q - t);
        if (len >= sizeof(b->tables[k].name)) len = sizeof(b->tables[k].name) - 1;
        memcpy(b->tables[k].name, t, len);
        b->tables[k].name[len] = 0;
        const char *s = lb_json_find(q, end, "stacks_bb");
        ok &= parse_numbers(s, b->tables[k].stacks, LB_TABLE_SIZE) == LB_TABLE_SIZE;
        t = q;
    }

    const char *cs = lb_json_find(txt, end, "cells");
    const char *ce = cs ? strstr(cs, "\"skipped_cells\"") : NULL;
    int ncells = 0;
    for (const char *c = cs; ok && c && c < ce; ) {
        c = strstr(c, "{\"hero_bucket\":");
        if (!c || c > ce) break;
        const char *e = strchr(c, '}');
        double hb = -1, vb = -1;
        if (lb_json_number(c, e, "hero_bucket", &hb) || lb_json_number(c, e, "villain_bucket", &vb) ||
            hb < 0 || hb > 3 || vb < 0 || vb > 3) { ok = 0; break; }
        LbCell *cell = &b->cells[(int)hb][(int)vb];
        const char *sk = lb_json_find(c, e, "skipped");
        cell->skipped = sk && !strncmp(sk, "true", 4);
        if (!cell->skipped) {
            const char *tn = lb_json_find(c, e, "table");
            cell->table = -1;
            for (int k = 0; k < LB_NUM_TABLES; k++) {
                size_t l = strlen(b->tables[k].name);
                if (tn && tn[0] == '"' && !strncmp(tn + 1, b->tables[k].name, l) && tn[1 + l] == '"') cell->table = k;
            }
            ok &= cell->table >= 0;
            ok &= lb_json_number(c, e, "hero_seat", &tmp[0]) == 0 && lb_json_number(c, e, "villain_seat", &tmp[1]) == 0;
            cell->hero_seat = (int)tmp[0];
            cell->villain_seat = (int)tmp[1];
            lb_json_number(c, e, "hero_premium", &cell->hero_premium);
            lb_json_number(c, e, "villain_premium", &cell->villain_premium);
        }
        ncells++;
        c = e;
    }
    free(txt);
    if (!ok || ncells != 16) { fprintf(stderr, "bad buckets file %s (%d cells)\n", path, ncells); return -1; }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Manifest                                                                 */

static int tier_of(int spr_idx, int flop_k)
{
    if (flop_k == 0) return spr_idx + 1;
    return spr_idx < 2 ? 4 : 5;
}

static void solve_id(char *out, size_t n, int pot, int spr, const char *flop, const LbBuckets *b, int table, int oop, int ip)
{
    if (table < 0) {
        snprintf(out, n, "%s-spr%s-%s-chip", LB_POT_NAME[pot], SPR_TAG[spr], flop);
    } else {
        const char *tn = strstr(b->tables[table].name, "runaway") ? "rcl" : "cl";
        snprintf(out, n, "%s-spr%s-%s-%s-o%di%d", LB_POT_NAME[pot], SPR_TAG[spr], flop, tn, oop, ip);
    }
}

int lb_make_manifest(const LbBuckets *b, LbManifest *m)
{
    memset(m, 0, sizeof(*m));
    for (int tier = 1; tier <= 5; tier++)
        for (int pot = 0; pot < 2; pot++)
            for (int spr = 0; spr < LB_NUM_SPR; spr++)
                for (int cls = 0; cls < FLOP_NUM_CLASSES; cls++)
                    for (int k = 0; k < 3; k++) {
                        if (tier_of(spr, k) != tier) continue;
                        const char *flop = flop_representative(cls, k);
                        int first = m->nsolves;
                        if (m->nsolves >= LB_MAX_SOLVES) return -1;
                        LbSolve *ch = &m->solves[m->nsolves++];
                        solve_id(ch->id, sizeof ch->id, pot, spr, flop, b, -1, -1, -1);
                        ch->tier = tier; ch->pot_type = pot; ch->spr_idx = spr; ch->flop_class = cls; ch->flop_k = k;
                        snprintf(ch->flop, sizeof ch->flop, "%s", flop);
                        ch->icm = 0; ch->table = -1; ch->oop_seat = ch->ip_seat = -1; ch->chip_solve = -1;
                        for (int hb = 0; hb < 4; hb++)
                            for (int vb = 0; vb < 4; vb++) {
                                const LbCell *cell = &b->cells[hb][vb];
                                if (cell->skipped) continue;
                                for (int pos = 0; pos < 2; pos++) {
                                    int oop = pos == LB_HERO_OOP ? cell->hero_seat : cell->villain_seat;
                                    int ip = pos == LB_HERO_OOP ? cell->villain_seat : cell->hero_seat;
                                    char id[64];
                                    solve_id(id, sizeof id, pot, spr, flop, b, cell->table, oop, ip);
                                    int si = -1;
                                    for (int j = first; j < m->nsolves; j++)
                                        if (!strcmp(m->solves[j].id, id)) si = j;
                                    if (si < 0) {
                                        if (m->nsolves >= LB_MAX_SOLVES) return -1;
                                        si = m->nsolves++;
                                        LbSolve *s = &m->solves[si];
                                        snprintf(s->id, sizeof s->id, "%s", id);
                                        s->tier = tier; s->pot_type = pot; s->spr_idx = spr; s->flop_class = cls; s->flop_k = k;
                                        snprintf(s->flop, sizeof s->flop, "%s", flop);
                                        s->icm = 1; s->table = cell->table; s->oop_seat = oop; s->ip_seat = ip;
                                        s->chip_solve = first;
                                    }
                                    if (m->nentries >= LB_MAX_ENTRIES) return -1;
                                    LbEntry *e = &m->entries[m->nentries++];
                                    e->tier = tier; e->hero_bucket = hb; e->villain_bucket = vb; e->position = pos;
                                    e->pot_type = pot; e->spr_idx = spr; e->flop_class = cls; e->flop_k = k;
                                    e->solve = si;
                                    e->hero_player = pos == LB_HERO_OOP ? 0 : 1;
                                    e->chip_solve = first;
                                }
                            }
                    }
    return 0;
}

int lb_write_manifest(const LbManifest *m, const char *dir)
{
    char path[512];
    lb_mkdirs(dir);
    snprintf(path, sizeof path, "%s/solves.tsv", dir);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "id\ttier\tpot_type\tspr\tflop\tflop_class\tflop_index\tmode\ttable\toop_seat\tip_seat\twarm_from\n");
    for (int i = 0; i < m->nsolves; i++) {
        const LbSolve *s = &m->solves[i];
        fprintf(f, "%s\tT%d\t%s\t%s\t%s\t%s\t%d\t%s\t%d\t%d\t%d\t%s\n", s->id, s->tier, LB_POT_NAME[s->pot_type],
                SPR_FILE[s->spr_idx], s->flop, flop_class_name(s->flop_class), s->flop_k + 1, s->icm ? "icm" : "chip",
                s->table, s->oop_seat, s->ip_seat, s->chip_solve >= 0 ? m->solves[s->chip_solve].id : "-");
    }
    fclose(f);
    snprintf(path, sizeof path, "%s/entries.tsv", dir);
    f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "tier\thero_bucket\tvillain_bucket\tposition\tpot_type\tspr\tflop_class\tflop_index\tsolve\thero_player\tchip_solve\n");
    for (int i = 0; i < m->nentries; i++) {
        const LbEntry *e = &m->entries[i];
        fprintf(f, "T%d\t%d\t%d\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\n", e->tier, e->hero_bucket, e->villain_bucket,
                e->position == LB_HERO_IP ? "ip" : "oop", LB_POT_NAME[e->pot_type], SPR_FILE[e->spr_idx],
                flop_class_name(e->flop_class), e->flop_k + 1, m->solves[e->solve].id, e->hero_player ? "ip" : "oop",
                m->solves[e->chip_solve].id);
    }
    fclose(f);
    return 0;
}

int lb_find_solve(const LbManifest *m, const char *id)
{
    for (int i = 0; i < m->nsolves; i++)
        if (!strcmp(m->solves[i].id, id)) return i;
    return -1;
}

static int index_of(const char *const *names, int n, const char *s)
{
    for (int i = 0; i < n; i++) if (!strcmp(names[i], s)) return i;
    return -1;
}

int lb_read_manifest(LbManifest *m, const char *dir)
{
    char path[512], line[512];
    memset(m, 0, sizeof(*m));
    static const char *class_names[FLOP_NUM_CLASSES];
    for (int i = 0; i < FLOP_NUM_CLASSES; i++) class_names[i] = flop_class_name(i);

    snprintf(path, sizeof path, "%s/solves.tsv", dir);
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot read %s\n", path); return -1; }
    if (!fgets(line, sizeof line, f)) { fclose(f); return -1; }
    char warm[LB_MAX_SOLVES][64];
    while (fgets(line, sizeof line, f) && m->nsolves < LB_MAX_SOLVES) {
        LbSolve *s = &m->solves[m->nsolves];
        char pot[8], spr[8], cls[32], mode[8];
        char *tok[12];
        int nt = 0;
        line[strcspn(line, "\n")] = 0;
        for (char *p = strtok(line, "\t"); p && nt < 12; p = strtok(NULL, "\t")) tok[nt++] = p;
        if (nt != 12) { fclose(f); return -1; }
        snprintf(s->id, sizeof s->id, "%s", tok[0]);
        s->tier = atoi(tok[1] + 1);
        snprintf(pot, sizeof pot, "%s", tok[2]);
        snprintf(spr, sizeof spr, "%s", tok[3]);
        snprintf(s->flop, sizeof s->flop, "%s", tok[4]);
        snprintf(cls, sizeof cls, "%s", tok[5]);
        s->flop_k = atoi(tok[6]) - 1;
        snprintf(mode, sizeof mode, "%s", tok[7]);
        s->table = atoi(tok[8]);
        s->oop_seat = atoi(tok[9]);
        s->ip_seat = atoi(tok[10]);
        snprintf(warm[m->nsolves], 64, "%s", tok[11]);
        s->pot_type = index_of(LB_POT_NAME, 2, pot);
        s->spr_idx = index_of(SPR_FILE, LB_NUM_SPR, spr);
        s->flop_class = index_of(class_names, FLOP_NUM_CLASSES, cls);
        s->icm = !strcmp(mode, "icm");
        if (s->pot_type < 0 || s->spr_idx < 0 || s->flop_class < 0) { fclose(f); return -1; }
        m->nsolves++;
    }
    fclose(f);
    for (int i = 0; i < m->nsolves; i++)
        m->solves[i].chip_solve = strcmp(warm[i], "-") ? lb_find_solve(m, warm[i]) : -1;

    snprintf(path, sizeof path, "%s/entries.tsv", dir);
    f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot read %s\n", path); return -1; }
    if (!fgets(line, sizeof line, f)) { fclose(f); return -1; }
    while (fgets(line, sizeof line, f) && m->nentries < LB_MAX_ENTRIES) {
        LbEntry *e = &m->entries[m->nentries];
        char *tok[11];
        int nt = 0;
        line[strcspn(line, "\n")] = 0;
        for (char *p = strtok(line, "\t"); p && nt < 11; p = strtok(NULL, "\t")) tok[nt++] = p;
        if (nt != 11) { fclose(f); return -1; }
        e->tier = atoi(tok[0] + 1);
        e->hero_bucket = atoi(tok[1]);
        e->villain_bucket = atoi(tok[2]);
        e->position = !strcmp(tok[3], "oop") ? LB_HERO_OOP : LB_HERO_IP;
        e->pot_type = index_of(LB_POT_NAME, 2, tok[4]);
        e->spr_idx = index_of(SPR_FILE, LB_NUM_SPR, tok[5]);
        e->flop_class = index_of(class_names, FLOP_NUM_CLASSES, tok[6]);
        e->flop_k = atoi(tok[7]) - 1;
        e->solve = lb_find_solve(m, tok[8]);
        e->hero_player = !strcmp(tok[9], "ip");
        e->chip_solve = lb_find_solve(m, tok[10]);
        if (e->solve < 0 || e->chip_solve < 0 || e->pot_type < 0 || e->spr_idx < 0 || e->flop_class < 0) {
            fclose(f);
            return -1;
        }
        m->nentries++;
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Spot configuration                                                       */

int lb_spot_config(const LbBuckets *b, const LbSolve *s, ss_config *c, float *ranges, double *stacks_out)
{
    char path[256];
    snprintf(path, sizeof path, "ranges/%s_spr%s.json", LB_POT_NAME[s->pot_type], SPR_FILE[s->spr_idx]);
    if (load_range(path, "bb", ranges) || load_range(path, "btn", ranges + NUM_COMBOS)) return -1;

    ss_config_default(c);
    c->board_len = cards_parse(s->flop, c->board, 5);
    c->range[0] = ranges;
    c->range[1] = ranges + NUM_COMBOS;
    double pot = LB_POT[s->pot_type];
    double eff = LB_SPR[s->spr_idx] * pot;
    c->pot = pot;
    c->stack = eff;
    c->threads = 1;
    c->max_iterations = 1000;
    c->target_exploitability_pct = 0.5;
    c->check_every = 10;
    c->compact_storage = 2;
    c->memory_limit = (size_t)4096 << 20;
    if (stacks_out) for (int i = 0; i < LB_TABLE_SIZE; i++) stacks_out[i] = 0.0;
    if (!s->icm) return 0;

    /* Scale the whole canonical table so the shorter player of the pair has exactly
       the effective stack behind after putting in half the pot. ICM and the risk
       premium only depend on stack proportions, so the matchup keeps its pressure. */
    const LbTable *t = &b->tables[s->table];
    double shorter = t->stacks[s->oop_seat] < t->stacks[s->ip_seat] ? t->stacks[s->oop_seat] : t->stacks[s->ip_seat];
    double k = (eff + pot / 2.0) / shorter;
    c->mode = SS_MODE_ICM;
    c->icm_players = LB_TABLE_SIZE;
    c->icm_num_payouts = LB_TABLE_SIZE;
    for (int i = 0; i < LB_TABLE_SIZE; i++) {
        c->icm_stacks[i] = k * t->stacks[i];
        if (i == s->oop_seat || i == s->ip_seat) c->icm_stacks[i] -= pot / 2.0;
        c->icm_payouts[i] = b->payouts[i];
    }
    c->seat[0] = s->oop_seat;
    c->seat[1] = s->ip_seat;
    if (stacks_out) for (int i = 0; i < LB_TABLE_SIZE; i++) stacks_out[i] = c->icm_stacks[i];
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Files                                                                    */

int lb_mkdirs(const char *path)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            if (mkdir(buf, 0755) && errno != EEXIST) return -1;
            *p = '/';
        }
    if (mkdir(buf, 0755) && errno != EEXIST) return -1;
    return 0;
}

int lb_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int lb_write_gz_atomic(const char *path, const char *data, size_t len)
{
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp.%ld.%lu", path, (long)getpid(), (unsigned long)pthread_self());
    gzFile g = gzopen(tmp, "wb9");
    if (!g) return -1;
    int ok = gzwrite(g, data, (unsigned)len) == (int)len;
    if (gzclose(g) != Z_OK) ok = 0;
    if (!ok || rename(tmp, path) != 0) { remove(tmp); return -1; }
    return 0;
}

char *lb_read_gz(const char *path)
{
    gzFile g = gzopen(path, "rb");
    if (!g) return NULL;
    size_t cap = 1 << 16, len = 0;
    char *buf = malloc(cap);
    for (;;) {
        if (!buf) { gzclose(g); return NULL; }
        if (len + 4096 + 1 > cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); gzclose(g); return NULL; }
            buf = nb;
        }
        int r = gzread(g, buf + len, 4096);
        if (r < 0) { free(buf); gzclose(g); return NULL; }
        if (r == 0) break;
        len += (size_t)r;
    }
    gzclose(g);
    buf[len] = 0;
    return buf;
}
