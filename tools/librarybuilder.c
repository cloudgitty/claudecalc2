/*
 * Library builder (Linux / macOS, links zlib).
 *
 *   librarybuilder manifest                 write manifest/solves.tsv and manifest/entries.tsv
 *   librarybuilder run [options]            solve pending spots (resumable)
 *       --tiers 1,2       tiers to run, in order (default 1,2,3,4,5)
 *       --jobs N          solves at a time (default 4, single-threaded each)
 *       --budget-mb M     memory budget for running solves (default 13000)
 *       --stop-at EPOCH   start no new solve after this Unix time
 *       --max N           start at most N solves
 *       --only a,b,...    only these solve ids (in manifest order)
 *       --first a,b,...   run these solve ids first (in the given order), then the tiers
 *   librarybuilder test5 a,b,...           spec test 5 on finished solves (5+ per pot type)
 *   librarybuilder pack                     results/ -> library/LiteLibrary.bin and .json
 *   librarybuilder test6                    spec test 6 on library/LiteLibrary.bin
 *   librarybuilder status                   solved / left per tier, compute hours, exploitability
 *
 * Run from the repo root. Results go to results/<id>.json.gz (written atomically),
 * raw flop strategies to raw/<id>.bin, the log to logs/build.log.
 */
#include "lb.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RESULTS_DIR "results"
#define RAW_DIR "raw"
#define LOG_PATH "logs/build.log"
#define MANIFEST_DIR "manifest"
#define BUCKETS_PATH "config/buckets.json"
#define BIN_PATH "library/LiteLibrary.bin"
#define STATE_DIR "work/state"   /* saved chip-EV solver states for warm starts (not committed) */
#define JSON_PATH "library/LiteLibrary.json"

static const char *SIZE_NAME[3] = { "33", "75", "allin" };

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

static void utc_stamp(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tm);
}

/* Approximate solver memory (MB) by pot type and SPR (B1 early estimate, 16-bit
   storage above 4 GB); used only to keep the running set inside the budget. */
static const double MEM_MB[2][LB_NUM_SPR] = { { 520, 2250, 3700 }, { 200, 680, 1950 } };

/* ------------------------------------------------------------------------ */
/* Result file                                                              */

static int fmt_num(char *p, size_t n, double x)
{
    return isnan(x) ? snprintf(p, n, "null") : snprintf(p, n, "%.6g", x);
}

static size_t write_summary(char *p, size_t n, const LbSummary *s)
{
    size_t o = 0;
    char v[32];
    fmt_num(v, sizeof v, s->bet_pct);
    o += (size_t)snprintf(p + o, n - o, "{\"bet_pct\": %s, ", v);
    o += (size_t)snprintf(p + o, n - o, "\"bet_size\": %d, \"bet_size_name\": \"%s\", ", s->bet_size,
                          s->bet_size >= 0 ? SIZE_NAME[s->bet_size] : "none");
    fmt_num(v, sizeof v, s->check_raise_pct);
    o += (size_t)snprintf(p + o, n - o, "\"check_raise_pct\": %s, ", v);
    o += (size_t)snprintf(p + o, n - o, "\"stackoff_category\": %d, \"stackoff_name\": \"%s\", ", s->stackoff_category,
                          s->stackoff_category >= 0 ? hand_category_name(s->stackoff_category) : "none");
    fmt_num(v, sizeof v, s->bluff_pct);
    o += (size_t)snprintf(p + o, n - o, "\"bluff_pct\": %s", v);
    const double *arr[3] = { s->category_mass, s->category_bet_pct, s->category_call_allin_pct };
    const char *names[3] = { "category_pct", "category_bet_pct", "category_call_allin_pct" };
    for (int a = 0; a < 3; a++) {
        o += (size_t)snprintf(p + o, n - o, ", \"%s\": [", names[a]);
        for (int c = 0; c < HC_NUM_CATEGORIES; c++) {
            fmt_num(v, sizeof v, arr[a][c]);
            o += (size_t)snprintf(p + o, n - o, "%s%s", c ? ", " : "", v);
        }
        o += (size_t)snprintf(p + o, n - o, "]");
    }
    o += (size_t)snprintf(p + o, n - o, "}");
    return o;
}

/* ------------------------------------------------------------------------ */
/* Runner                                                                   */

enum { J_PENDING = 0, J_RUNNING, J_DONE, J_FAILED };

typedef struct {
    ss_solver *solver;    /* finished chip-EV solve kept as warm-start source */
    int waiting;          /* dependent ICM jobs of this run that have not copied it yet */
    double mem_mb;
    int on_disk;          /* a saved state exists in STATE_DIR (load it when first needed) */
} Holder;

typedef struct {
    const LbManifest *m;
    const LbBuckets *b;
    int *jobs;            /* solve indices in run order */
    int *state;
    int njobs;
    Holder *holders;      /* by solve index */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    double budget_mb, reserved_mb;
    int running, started, max_start;
    double stop_at;
    FILE *log;
    int done, failed;
} Runner;

/* ICM solves at SPR 3.5 and 6 warm-start from their chip-EV solve (B1: no gain at
   SPR 1.5). LB_WARM_ALL=1 also warm-starts SPR 1.5 (used to test the path quickly). */
static int g_warm_all;   /* set once in cmd_run before the workers start */

static int needs_warm(const LbSolve *s)
{
    return s->icm && (s->spr_idx >= 1 || g_warm_all) && s->chip_solve >= 0;
}

static void state_path(const Runner *R, int chip, char *out, size_t n)
{
    snprintf(out, n, "%s/%s.bin", STATE_DIR, R->m->solves[chip].id);
}

/* Rebuilds a chip-EV solver from its saved state (same configuration as when it ran). */
static ss_solver *load_saved(Runner *R, int chip)
{
    float *ranges = malloc(sizeof(float) * 2 * NUM_COMBOS);
    ss_config c;
    char err[256], path[256];
    if (!ranges || lb_spot_config(R->b, &R->m->solves[chip], &c, ranges, NULL)) { free(ranges); return NULL; }
    ss_solver *S = ss_create(&c, err, sizeof err);
    free(ranges);
    state_path(R, chip, path, sizeof path);
    if (S && ss_load_state(S, path)) { ss_destroy(S); S = NULL; }
    return S;
}

/* Takes the warm-start source (if any) and releases this job's claim on it.
   h - R->holders is the chip-EV solve index. */
static void release_holder(Runner *R, Holder *h, ss_solver *dst, int *warm_used)
{
    if (!h) return;
    int chip = (int)(h - R->holders);
    pthread_mutex_lock(&R->lock);
    if (dst && !h->solver && h->on_disk) {
        h->on_disk = 0;   /* one attempt */
        ss_solver *S = load_saved(R, chip);
        if (S) {
            h->solver = S;
            h->mem_mb = ss_memory_required(S) / 1048576.0;
            R->reserved_mb += h->mem_mb;
        }
    }
    if (dst && h->solver && ss_warm_start(dst, h->solver, 1.0) == 0) *warm_used = 1;
    if (--h->waiting <= 0) {
        if (h->solver) {
            ss_destroy(h->solver);
            h->solver = NULL;
            R->reserved_mb -= h->mem_mb;
        }
        char path[256];
        state_path(R, chip, path, sizeof path);
        remove(path);
        pthread_cond_broadcast(&R->cond);
    }
    pthread_mutex_unlock(&R->lock);
}

/* Returns 1 if the finished solver was kept as a warm-start source. *mem_mb is the
   job's reservation: an estimate on entry, replaced by the solver's real size once
   it exists (the caller releases *mem_mb). */
static int run_one(Runner *R, int solve, Holder *h, double *mem_mb, int *warm_used)
{
    const LbSolve *s = &R->m->solves[solve];
    float *ranges = malloc(sizeof(float) * 2 * NUM_COMBOS);
    ss_config c;
    double stacks[LB_TABLE_SIZE];
    char err[256], stamp[32];
    *warm_used = 0;
    if (!ranges || lb_spot_config(R->b, s, &c, ranges, stacks)) {
        fprintf(stderr, "%s: cannot configure\n", s->id);
        free(ranges);
        release_holder(R, h, NULL, warm_used);
        pthread_mutex_lock(&R->lock); R->state[solve] = J_FAILED; R->failed++; pthread_mutex_unlock(&R->lock);
        return 0;
    }
    ss_solver *S = ss_create(&c, err, sizeof err);
    if (!S) {
        fprintf(stderr, "%s: %s\n", s->id, err);
        free(ranges);
        release_holder(R, h, NULL, warm_used);
        pthread_mutex_lock(&R->lock); R->state[solve] = J_FAILED; R->failed++; pthread_mutex_unlock(&R->lock);
        return 0;
    }
    pthread_mutex_lock(&R->lock);
    double actual = ss_memory_required(S) / 1048576.0;
    R->reserved_mb += actual - *mem_mb;
    *mem_mb = actual;
    pthread_cond_broadcast(&R->cond);
    pthread_mutex_unlock(&R->lock);
    release_holder(R, h, S, warm_used);
    ss_result r;
    ss_solve(S, &r);

    uint8_t flop[3];
    cards_parse(s->flop, flop, 3);
    LbSummary sum[2];
    lb_summarise(S, flop, ranges, 0, &sum[0]);
    lb_summarise(S, flop, ranges, 1, &sum[1]);

    char raw_path[256];
    size_t raw_bytes = 0;
    snprintf(raw_path, sizeof raw_path, "%s/%s.bin", RAW_DIR, s->id);
    if (lb_write_raw(S, raw_path, &raw_bytes)) fprintf(stderr, "%s: raw output failed\n", s->id);

    const uint16_t *cb;
    int n0 = ss_player_combos(S, 0, &cb), n1 = ss_player_combos(S, 1, &cb);
    size_t cap = 16384, o = 0;
    char *js = malloc(cap);
    if (js) {
        utc_stamp(stamp, sizeof stamp);
        o += (size_t)snprintf(js + o, cap - o,
                              "{\"id\": \"%s\", \"tier\": %d, \"pot_type\": \"%s\", \"spr\": %g, \"flop\": \"%s\", "
                              "\"flop_class\": \"%s\", \"flop_index\": %d, \"mode\": \"%s\", ",
                              s->id, s->tier, LB_POT_NAME[s->pot_type], LB_SPR[s->spr_idx], s->flop,
                              flop_class_name(s->flop_class), s->flop_k + 1, s->icm ? "icm" : "chip");
        if (s->icm) {
            o += (size_t)snprintf(js + o, cap - o, "\"table\": \"%s\", \"oop_seat\": %d, \"ip_seat\": %d, \"stacks_behind\": [",
                                  R->b->tables[s->table].name, s->oop_seat, s->ip_seat);
            for (int i = 0; i < LB_TABLE_SIZE; i++) o += (size_t)snprintf(js + o, cap - o, "%s%.4f", i ? ", " : "", stacks[i]);
            o += (size_t)snprintf(js + o, cap - o, "], \"payouts\": [");
            for (int i = 0; i < LB_TABLE_SIZE; i++) o += (size_t)snprintf(js + o, cap - o, "%s%g", i ? ", " : "", R->b->payouts[i]);
            o += (size_t)snprintf(js + o, cap - o, "], ");
        }
        o += (size_t)snprintf(js + o, cap - o,
                              "\"pot\": %g, \"effective_stack\": %g, \"combos\": [%d, %d], \"warm_start\": %s, "
                              "\"iterations\": %d, \"exploitability_pct\": [%.4f, %.4f], \"value\": [%.6g, %.6g], "
                              "\"pot_value\": [%.6g, %.6g], \"seconds\": %.2f, \"peak_memory_mb\": %.1f, "
                              "\"storage\": \"%s\", \"raw_bytes\": %zu, \"finished_utc\": \"%s\", \"players\": [",
                              c.pot, c.stack, n0, n1, *warm_used ? "true" : "false", r.iterations,
                              r.exploitability_pct[0], r.exploitability_pct[1], r.value[0], r.value[1], r.pot_value[0],
                              r.pot_value[1], r.seconds, r.peak_memory / 1048576.0, r.compact_storage ? "16-bit" : "float32",
                              raw_bytes, stamp);
        o += write_summary(js + o, cap - o, &sum[0]);
        o += (size_t)snprintf(js + o, cap - o, ", ");
        o += write_summary(js + o, cap - o, &sum[1]);
        o += (size_t)snprintf(js + o, cap - o, "]}\n");
        char path[256];
        snprintf(path, sizeof path, "%s/%s.json.gz", RESULTS_DIR, s->id);
        if (lb_write_gz_atomic(path, js, o)) fprintf(stderr, "%s: cannot write result\n", s->id);
        free(js);
    }

    int keep = 0;
    if (!s->icm && R->holders[solve].waiting > 0) {
        /* save the state so warm starts survive a restart or crash */
        char path[256];
        state_path(R, solve, path, sizeof path);
        if (ss_save_state(S, path)) { fprintf(stderr, "%s: cannot save state\n", s->id); remove(path); }
    }
    pthread_mutex_lock(&R->lock);
    Holder *own = &R->holders[solve];
    if (!s->icm && own->waiting > 0) { own->solver = S; own->mem_mb = *mem_mb; keep = 1; }
    R->state[solve] = J_DONE;
    R->done++;
    char line[512];
    utc_stamp(stamp, sizeof stamp);
    snprintf(line, sizeof line, "%s %s T%d iterations %d exploitability %.3f%% / %.3f%% seconds %.1f memory %.0fMB raw %.1fKB%s",
             stamp, s->id, s->tier, r.iterations, r.exploitability_pct[0], r.exploitability_pct[1], r.seconds,
             r.peak_memory / 1048576.0, raw_bytes / 1024.0, *warm_used ? " warm" : "");
    printf("%s\n", line);
    fflush(stdout);
    if (R->log) { fprintf(R->log, "%s\n", line); fflush(R->log); }
    pthread_mutex_unlock(&R->lock);
    if (!keep) ss_destroy(S);
    free(ranges);
    return keep;
}

static void *worker(void *arg)
{
    Runner *R = arg;
    pthread_mutex_lock(&R->lock);
    for (;;) {
        int stop = (R->stop_at > 0 && now_s() >= R->stop_at) || (R->max_start > 0 && R->started >= R->max_start);
        int pick = -1, pending = 0;
        if (!stop)
            for (int j = 0; j < R->njobs; j++) {
                int si = R->jobs[j];
                if (R->state[si] != J_PENDING) continue;
                pending++;
                const LbSolve *s = &R->m->solves[si];
                if (needs_warm(s) && R->state[s->chip_solve] != J_DONE && R->state[s->chip_solve] != J_FAILED &&
                    R->state[s->chip_solve] != -1)
                    continue;   /* wait for its chip-EV solve */
                double mem = MEM_MB[s->pot_type][s->spr_idx];
                if (R->running > 0 && R->reserved_mb + mem > R->budget_mb) continue;
                pick = si;
                break;
            }
        if (pick < 0) {
            if (stop || pending == 0) break;
            pthread_cond_wait(&R->cond, &R->lock);
            continue;
        }
        const LbSolve *s = &R->m->solves[pick];
        double mem = MEM_MB[s->pot_type][s->spr_idx];
        {
            /* solved meanwhile by another process: skip */
            char path[256];
            snprintf(path, sizeof path, "%s/%s.json.gz", RESULTS_DIR, s->id);
            if (lb_file_exists(path)) {
                R->state[pick] = J_DONE;
                if (needs_warm(s) && R->holders[s->chip_solve].waiting > 0) {
                    Holder *hs = &R->holders[s->chip_solve];
                    if (--hs->waiting <= 0) {
                        if (hs->solver) {
                            ss_destroy(hs->solver);
                            hs->solver = NULL;
                            R->reserved_mb -= hs->mem_mb;
                        }
                        char sp[256];
                        state_path(R, s->chip_solve, sp, sizeof sp);
                        remove(sp);
                    }
                }
                pthread_cond_broadcast(&R->cond);
                continue;
            }
        }
        R->state[pick] = J_RUNNING;
        R->running++;
        R->started++;
        R->reserved_mb += mem;
        Holder *h = needs_warm(s) ? &R->holders[s->chip_solve] : NULL;
        if (h && R->state[s->chip_solve] == -1 && !h->on_disk && !h->solver) h = NULL;   /* no source: cold start */
        pthread_mutex_unlock(&R->lock);

        int used = 0;
        int kept = run_one(R, pick, h, &mem, &used);

        pthread_mutex_lock(&R->lock);
        if (!kept) R->reserved_mb -= mem;   /* a kept chip-EV solver stays reserved until released */
        R->running--;
        pthread_cond_broadcast(&R->cond);
    }
    pthread_cond_broadcast(&R->cond);
    pthread_mutex_unlock(&R->lock);
    return NULL;
}

static int parse_list(const char *s, int *out, int max)
{
    int n = 0;
    while (*s && n < max) {
        out[n++] = atoi(s);
        while (*s && *s != ',') s++;
        if (*s == ',') s++;
    }
    return n;
}

static int in_list(const char *list, const char *id)
{
    size_t l = strlen(id);
    for (const char *p = list; (p = strstr(p, id)) != NULL; p += l)
        if ((p == list || p[-1] == ',') && (p[l] == 0 || p[l] == ',')) return 1;
    return 0;
}

static int cmd_run(int argc, char **argv)
{
    int tiers[5] = { 1, 2, 3, 4, 5 }, ntiers = 5, jobs = 4, max_start = 0;
    double budget = 13000, stop_at = 0;
    const char *only = NULL, *first = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--tiers") && i + 1 < argc) ntiers = parse_list(argv[++i], tiers, 5);
        else if (!strcmp(argv[i], "--jobs") && i + 1 < argc) jobs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--budget-mb") && i + 1 < argc) budget = atof(argv[++i]);
        else if (!strcmp(argv[i], "--stop-at") && i + 1 < argc) stop_at = atof(argv[++i]);
        else if (!strcmp(argv[i], "--max") && i + 1 < argc) max_start = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--first") && i + 1 < argc) first = argv[++i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (jobs < 1) jobs = 1;
    g_warm_all = getenv("LB_WARM_ALL") && atoi(getenv("LB_WARM_ALL"));
    LbBuckets b;
    LbManifest *m = calloc(1, sizeof(LbManifest));
    if (!m || lb_load_buckets(BUCKETS_PATH, &b) || lb_read_manifest(m, MANIFEST_DIR)) { free(m); return 1; }
    lb_mkdirs(RESULTS_DIR);
    lb_mkdirs(RAW_DIR);
    lb_mkdirs("logs");
    handeval_init();

    Runner R;
    memset(&R, 0, sizeof R);
    R.m = m;
    R.b = &b;
    R.jobs = calloc((size_t)m->nsolves, sizeof(int));
    R.state = calloc((size_t)m->nsolves, sizeof(int));
    R.holders = calloc((size_t)m->nsolves, sizeof(Holder));
    if (!R.jobs || !R.state || !R.holders) return 1;
    /* state -1: not part of this run (already solved or filtered out) */
    for (int i = 0; i < m->nsolves; i++) R.state[i] = -1;
    int skipped = 0;
    if (first) {
        /* priority list, in the given order */
        const char *p = first;
        while (*p) {
            char id[64];
            size_t l = strcspn(p, ",");
            if (l >= sizeof id) l = sizeof id - 1;
            memcpy(id, p, l);
            id[l] = 0;
            p += l;
            if (*p == ',') p++;
            int si = lb_find_solve(m, id);
            if (si < 0) { fprintf(stderr, "unknown solve id %s\n", id); continue; }
            if (R.state[si] != -1) continue;
            char path[256];
            snprintf(path, sizeof path, "%s/%s.json.gz", RESULTS_DIR, id);
            if (lb_file_exists(path)) { skipped++; continue; }
            R.jobs[R.njobs++] = si;
            R.state[si] = J_PENDING;
        }
    }
    for (int t = 0; t < ntiers; t++)
        for (int i = 0; i < m->nsolves; i++) {
            const LbSolve *s = &m->solves[i];
            if (s->tier != tiers[t]) continue;
            if (R.state[i] != -1) continue;   /* already queued by --first */
            if (only && !in_list(only, s->id)) continue;
            char path[256];
            snprintf(path, sizeof path, "%s/%s.json.gz", RESULTS_DIR, s->id);
            if (lb_file_exists(path)) { skipped++; continue; }
            R.jobs[R.njobs++] = i;
            R.state[i] = J_PENDING;
        }
    lb_mkdirs(STATE_DIR);
    for (int j = 0; j < R.njobs; j++) {
        const LbSolve *s = &m->solves[R.jobs[j]];
        if (!needs_warm(s)) continue;
        Holder *h = &R.holders[s->chip_solve];
        if (R.state[s->chip_solve] == J_PENDING) h->waiting++;
        else if (R.state[s->chip_solve] == -1) {
            /* chip-EV solved in an earlier run: warm start from its saved state if any */
            char path[256];
            snprintf(path, sizeof path, "%s/%s.bin", STATE_DIR, m->solves[s->chip_solve].id);
            if (lb_file_exists(path)) { h->on_disk = 1; h->waiting++; }
        }
    }
    printf("librarybuilder: %d solves to run, %d already solved, %d jobs at a time, budget %.0f MB\n", R.njobs, skipped,
           jobs, budget);
    fflush(stdout);
    R.budget_mb = budget;
    R.stop_at = stop_at;
    R.max_start = max_start;
    R.log = fopen(LOG_PATH, "a");
    pthread_mutex_init(&R.lock, NULL);
    pthread_cond_init(&R.cond, NULL);
    pthread_t th[64];
    if (jobs > 64) jobs = 64;
    for (int i = 0; i < jobs; i++) pthread_create(&th[i], NULL, worker, &R);
    for (int i = 0; i < jobs; i++) pthread_join(th[i], NULL);
    for (int i = 0; i < m->nsolves; i++) ss_destroy(R.holders[i].solver);
    printf("librarybuilder: %d solved, %d failed\n", R.done, R.failed);
    if (R.log) fclose(R.log);
    pthread_mutex_destroy(&R.lock);
    pthread_cond_destroy(&R.cond);
    free(R.jobs);
    free(R.state);
    free(R.holders);
    free(m);
    return R.failed ? 1 : 0;
}

/* ------------------------------------------------------------------------ */

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static int cmd_status(void)
{
    LbManifest *m = calloc(1, sizeof(LbManifest));
    if (!m || lb_read_manifest(m, MANIFEST_DIR)) { free(m); return 1; }
    int solved[6] = { 0 }, total[6] = { 0 }, esolved[6] = { 0 }, etotal[6] = { 0 };
    double secs[6] = { 0 }, *expl = calloc((size_t)m->nsolves, sizeof(double)), *tsec[6];
    int nexpl = 0, ntsec[6] = { 0 };
    char *done = calloc((size_t)m->nsolves, 1);
    for (int t = 0; t < 6; t++) tsec[t] = calloc((size_t)m->nsolves, sizeof(double));
    double peak_mb = 0;
    for (int i = 0; i < m->nsolves; i++) {
        const LbSolve *s = &m->solves[i];
        total[s->tier]++;
        char path[256];
        snprintf(path, sizeof path, "%s/%s.json.gz", RESULTS_DIR, s->id);
        char *js = lb_file_exists(path) ? lb_read_gz(path) : NULL;
        if (!js) continue;
        const char *end = js + strlen(js);
        double sec = 0, mem = 0;
        lb_json_number(js, end, "seconds", &sec);
        lb_json_number(js, end, "peak_memory_mb", &mem);
        const char *e = lb_json_find(js, end, "exploitability_pct");
        double e0 = 0, e1 = 0;
        if (e) sscanf(e, "[%lf, %lf]", &e0, &e1);
        expl[nexpl++] = e0 > e1 ? e0 : e1;
        solved[s->tier]++;
        secs[s->tier] += sec;
        tsec[s->tier][ntsec[s->tier]++] = sec;
        if (mem > peak_mb) peak_mb = mem;
        done[i] = 1;
        free(js);
    }
    for (int i = 0; i < m->nentries; i++) {
        etotal[m->entries[i].tier]++;
        if (done[m->entries[i].solve]) esolved[m->entries[i].tier]++;
    }
    qsort(expl, (size_t)nexpl, sizeof(double), cmp_double);
    double hours = 0;
    printf("tier | solves done / total | library entries done / total | compute h (1 core) | median s | slowest s\n");
    for (int t = 1; t <= 5; t++) {
        qsort(tsec[t], (size_t)ntsec[t], sizeof(double), cmp_double);
        double med = ntsec[t] ? tsec[t][ntsec[t] / 2] : 0, slow = ntsec[t] ? tsec[t][ntsec[t] - 1] : 0;
        printf("T%d | %d / %d | %d / %d | %.2f | %.0f | %.0f\n", t, solved[t], total[t], esolved[t], etotal[t], secs[t] / 3600.0,
               med, slow);
        hours += secs[t] / 3600.0;
    }
    printf("total compute %.2f core-hours; median exploitability (max of both players) %.3f%% over %d solves; "
           "peak solver memory %.0f MB\n",
           hours, nexpl ? expl[nexpl / 2] : 0.0, nexpl, peak_mb);
    for (int t = 0; t < 6; t++) free(tsec[t]);
    free(expl);
    free(done);
    free(m);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: librarybuilder manifest|run|pack|test6|status\n"); return 2; }
    if (!strcmp(argv[1], "manifest")) {
        LbBuckets b;
        LbManifest *m = calloc(1, sizeof(LbManifest));
        if (!m || lb_load_buckets(BUCKETS_PATH, &b) || lb_make_manifest(&b, m) || lb_write_manifest(m, MANIFEST_DIR)) {
            free(m);
            return 1;
        }
        int per_tier[6] = { 0 }, icm[6] = { 0 }, ent[6] = { 0 };
        for (int i = 0; i < m->nsolves; i++) { per_tier[m->solves[i].tier]++; icm[m->solves[i].tier] += m->solves[i].icm; }
        for (int i = 0; i < m->nentries; i++) ent[m->entries[i].tier]++;
        for (int t = 1; t <= 5; t++)
            printf("T%d: %d solves (%d ICM + %d chip EV), %d library entries\n", t, per_tier[t], icm[t], per_tier[t] - icm[t], ent[t]);
        printf("total %d solves, %d entries\n", m->nsolves, m->nentries);
        free(m);
        return 0;
    }
    if (!strcmp(argv[1], "run")) return cmd_run(argc, argv);
    if (!strcmp(argv[1], "test5")) {
        /* Spec test 5: exploitability reaches the target on (at least) 5 sample spots
           of each pot type. Checks the finished results of the listed solves. */
        if (argc < 3) { fprintf(stderr, "usage: librarybuilder test5 id,id,...\n"); return 2; }
        LbManifest *m = calloc(1, sizeof(LbManifest));
        if (!m || lb_read_manifest(m, MANIFEST_DIR)) { free(m); return 1; }
        int ok_count[2] = { 0, 0 }, bad = 0;
        for (int i = 0; i < m->nsolves; i++) {
            const LbSolve *s = &m->solves[i];
            if (!in_list(argv[2], s->id)) continue;
            char path[256];
            snprintf(path, sizeof path, "%s/%s.json.gz", RESULTS_DIR, s->id);
            char *js = lb_file_exists(path) ? lb_read_gz(path) : NULL;
            if (!js) { printf("  %-34s not solved\n", s->id); bad++; continue; }
            const char *end = js + strlen(js), *e = lb_json_find(js, end, "exploitability_pct");
            double e0 = 99, e1 = 99, it = 0;
            if (e) sscanf(e, "[%lf, %lf]", &e0, &e1);
            lb_json_number(js, end, "iterations", &it);
            int pass = e0 <= 0.5 && e1 <= 0.5 && it < 1000;
            printf("  %-34s %-3s SPR %-3g %s iterations %4.0f exploitability %.3f%% / %.3f%% %s\n", s->id,
                   LB_POT_NAME[s->pot_type], LB_SPR[s->spr_idx], s->icm ? "ICM " : "chip", it, e0, e1, pass ? "ok" : "FAIL");
            if (pass) ok_count[s->pot_type]++; else bad++;
            free(js);
        }
        int pass = bad == 0 && ok_count[0] >= 5 && ok_count[1] >= 5;
        printf("test 5: %d single-raised and %d 3-bet spots reached 0.5%%, %d failed: %s\n", ok_count[0], ok_count[1], bad,
               pass ? "PASS" : "FAIL");
        free(m);
        return pass ? 0 : 1;
    }
    if (!strcmp(argv[1], "status")) return cmd_status();
    if (!strcmp(argv[1], "pack") || !strcmp(argv[1], "test6")) {
        LbBuckets b;
        LbManifest *m = calloc(1, sizeof(LbManifest));
        if (!m || lb_load_buckets(BUCKETS_PATH, &b) || lb_read_manifest(m, MANIFEST_DIR)) { free(m); return 1; }
        int rc = 0;
        if (!strcmp(argv[1], "pack")) {
            int n = 0;
            size_t size = 0;
            lb_mkdirs("library");
            rc = lb_pack(m, &b, RESULTS_DIR, BIN_PATH, JSON_PATH, &n, &size);
            printf("packed %d entries into %s (%zu bytes)%s\n", n, BIN_PATH, size, rc ? " FAILED" : "");
        } else {
            int checked = 0;
            int failures = lb_check_pack(m, RESULTS_DIR, BIN_PATH, &checked, 1);
            printf("test 6: %d values checked, %d mismatches: %s\n", checked, failures, failures == 0 && checked > 0 ? "PASS" : "FAIL");
            rc = failures != 0 || checked == 0;
        }
        free(m);
        return rc;
    }
    fprintf(stderr, "unknown command %s\n", argv[1]);
    return 2;
}
