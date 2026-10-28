#ifndef ICMCORE_LIBREADER_H
#define ICMCORE_LIBREADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reader for the packed Lite library (LiteLibrary.bin). The file format is
 * documented in LIBRARY_FORMAT.md at the repo root. Thread-safe after opening
 * (read-only); no global state.
 */

#define LR_FORMAT_VERSION 1
#define LR_VALUES_PER_SIDE 5

enum { LR_IN_POSITION = 0, LR_OUT_OF_POSITION = 1 };
enum { LR_POT_SINGLE_RAISED = 0, LR_POT_THREE_BET = 1 };
enum { LR_SIZE_33 = 0, LR_SIZE_75 = 1, LR_SIZE_ALLIN = 2 };
enum { LR_SOURCE_INTERPOLATED = 0, LR_SOURCE_NEAREST = 1 };

/* Order of the five stored values per side in the file. */
enum { LR_V_BET_PCT = 0, LR_V_BET_SIZE, LR_V_CHECK_RAISE_PCT, LR_V_STACKOFF, LR_V_BLUFF_PCT };

typedef struct {
    float bet_pct;            /* 0..100 */
    int bet_size;             /* LR_SIZE_*, or -1 when hero never bets */
    float check_raise_pct;    /* out of position only; negative when not applicable */
    int stack_off_category;   /* weakest hand category (handcat.h HC_*) stacking off >= 50%, or -1 */
    float bluff_pct;          /* share of the betting range in weak draws or air, 0..100 */
    int source;               /* LR_SOURCE_INTERPOLATED or LR_SOURCE_NEAREST (fallback) */
} lr_values;

typedef struct {
    uint8_t hero_bucket, villain_bucket;   /* 0..3 */
    uint8_t position;                      /* LR_IN_POSITION / LR_OUT_OF_POSITION */
    uint8_t pot_type;                      /* LR_POT_* */
    uint8_t spr_index;                     /* 0, 1, 2 = SPR 1.5, 3.5, 6 */
    uint8_t flop_class;                    /* flopclass.h FLOP_* */
    uint8_t flop_index;                    /* 0..2: representative flop 1..3 */
    uint8_t tier;                          /* 1..5 */
} lr_key;

typedef struct lr_library lr_library;

/* Copies and validates the data (magic, version, sizes, CRC-32). */
lr_library *lr_open_memory(const void *data, size_t len, char *err, size_t err_len);
void lr_close(lr_library *lib);

int lr_entry_count(const lr_library *lib);
/* Raw values of entry i (values in file order; NAN where empty). Returns 0. */
int lr_entry(const lr_library *lib, int i, lr_key *key, float icm[LR_VALUES_PER_SIDE], float chip[LR_VALUES_PER_SIDE]);
/* Bit t-1 set when tier t is complete. */
unsigned lr_tiers_complete(const lr_library *lib);
/* Bucket thresholds (3) and centres (4) stored in the file. */
void lr_buckets(const lr_library *lib, float thresholds[3], float centres[4]);

/*
 * Values for any spot. The flop is mapped to its class; values are averaged over
 * the solved representative flops of that class, interpolated bilinearly between
 * the nearest premium bucket centres and linearly between the nearest SPRs
 * (1.5, 3.5, 6). Corners with no solved spot are left out and the weights
 * renormalised; if none is solved, the nearest solved spot is used
 * (source = LR_SOURCE_NEAREST). Bet size and stack-off category come from the
 * corner with the largest weight. icm = 1 for ICM values, 0 for the chip-EV
 * baseline. Returns 0, or -1 if the library has nothing usable.
 */
int lr_query(const lr_library *lib, double hero_premium, double villain_premium, int position,
             int pot_type, double spr, const uint8_t flop[3], int icm, lr_values *out);

#ifdef __cplusplus
}
#endif

#endif
