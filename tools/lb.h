#ifndef TOOLS_LB_H
#define TOOLS_LB_H

/* Library builder: shared declarations (Linux / macOS only, links zlib). */

#include "icmcore.h"
#include <stddef.h>

#define LB_MAX_SOLVES 4096
#define LB_MAX_ENTRIES 8192
#define LB_NUM_TABLES 2
#define LB_TABLE_SIZE 6
#define LB_NUM_SPR 3

enum { LB_SRP = 0, LB_3BP = 1 };
enum { LB_HERO_IP = 0, LB_HERO_OOP = 1 };

extern const double LB_SPR[LB_NUM_SPR];       /* 1.5, 3.5, 6 */
extern const double LB_POT[2];                /* 6, 15 big blinds */
extern const char *LB_POT_NAME[2];            /* "srp", "3bp" */

typedef struct {
    char name[32];
    double stacks[LB_TABLE_SIZE];
} LbTable;

typedef struct {
    int skipped;
    int table, hero_seat, villain_seat;
    double hero_premium, villain_premium;
} LbCell;

typedef struct {
    double thresholds[3];
    double centres[4];
    double payouts[LB_TABLE_SIZE];
    LbTable tables[LB_NUM_TABLES];
    LbCell cells[4][4];
} LbBuckets;

/* One solve. ICM solves have a table and the seats of OOP and IP; chip-EV solves
   have table -1. */
typedef struct {
    char id[64];
    int tier;                /* 1..5 */
    int pot_type, spr_idx, flop_class, flop_k;
    char flop[8];
    int icm;
    int table, oop_seat, ip_seat;
    int chip_solve;          /* index of the chip-EV solve of the same spot (ICM solves), else -1 */
} LbSolve;

/* One library entry: what the app looks up. */
typedef struct {
    int tier;
    int hero_bucket, villain_bucket, position;   /* position: LB_HERO_IP / LB_HERO_OOP */
    int pot_type, spr_idx, flop_class, flop_k;
    int solve;               /* ICM solve index */
    int hero_player;         /* 0 = OOP, 1 = IP inside that solve */
    int chip_solve;
} LbEntry;

typedef struct {
    int nsolves, nentries;
    LbSolve solves[LB_MAX_SOLVES];
    LbEntry entries[LB_MAX_ENTRIES];
} LbManifest;

/* Per-player flop summary (spec "What each spot stores"). */
typedef struct {
    double bet_pct;              /* 0..100 */
    int bet_size;                /* LR_SIZE_33 / 75 / ALLIN, -1 if never bets */
    double check_raise_pct;      /* OOP only; NAN in position */
    int stackoff_category;       /* HC_*, -1 if none */
    double bluff_pct;            /* 0..100 */
    double category_mass[HC_NUM_CATEGORIES];      /* share of the range, % */
    double category_bet_pct[HC_NUM_CATEGORIES];   /* bet % by category */
    double category_call_allin_pct[HC_NUM_CATEGORIES];
} LbSummary;

/* buckets.json */
int lb_load_buckets(const char *path, LbBuckets *b);

/* manifest generation and files manifest/solves.tsv, manifest/entries.tsv */
int lb_make_manifest(const LbBuckets *b, LbManifest *m);
int lb_write_manifest(const LbManifest *m, const char *dir);
int lb_read_manifest(LbManifest *m, const char *dir);
int lb_find_solve(const LbManifest *m, const char *id);

/* Solver configuration for a solve (ranges must hold 2 x 1326 floats). */
int lb_spot_config(const LbBuckets *b, const LbSolve *s, ss_config *c, float *ranges, double *stacks_out);

/* Summaries and raw strategies from a finished solve. */
void lb_summarise(const ss_solver *s, const uint8_t flop[3], const float *ranges, int hero, LbSummary *out);
int lb_write_raw(const ss_solver *s, const char *path, size_t *bytes);

/* Small file helpers. */
int lb_mkdirs(const char *path);
int lb_file_exists(const char *path);
int lb_write_gz_atomic(const char *path, const char *data, size_t len);
char *lb_read_gz(const char *path);   /* malloc'd, NUL-terminated, NULL on error */

/* Minimal JSON lookups for the files this tool writes. */
const char *lb_json_find(const char *start, const char *end, const char *key);
int lb_json_number(const char *start, const char *end, const char *key, double *out);
/* Object of players[i] in a result file: sets [*os, *oe). */
int lb_json_player(const char *json, int i, const char **os, const char **oe);

/* Packing and test 6. */
int lb_pack(const LbManifest *m, const LbBuckets *b, const char *results_dir, const char *bin_path,
            const char *json_path, int *packed_entries, size_t *bin_size);
int lb_check_pack(const LbManifest *m, const char *results_dir, const char *bin_path, int *checked, int verbose);

#endif
