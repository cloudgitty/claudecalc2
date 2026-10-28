#ifndef ICMCORE_LIBREADER_H
#define ICMCORE_LIBREADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reader for the packed Lite library (LiteLibrary.bin).
 * Interface fixed in B1; the file format and the implementation are Part B2's
 * work (LIBRARY_FORMAT.md). Until then lr_open_memory() fails with a message.
 */

enum { LR_IN_POSITION = 0, LR_OUT_OF_POSITION = 1 };
enum { LR_POT_SINGLE_RAISED = 0, LR_POT_THREE_BET = 1 };
enum { LR_SIZE_33 = 0, LR_SIZE_75 = 1, LR_SIZE_ALLIN = 2 };

typedef struct {
    float bet_pct;            /* 0..100 */
    int bet_size;             /* LR_SIZE_* */
    float check_raise_pct;    /* out of position only; negative when not applicable */
    int stack_off_category;   /* weakest hand category stacking off >= 50%, or -1 */
    float bluff_pct;          /* share of the betting range in weak draws or air */
} lr_values;

typedef struct lr_library lr_library;

lr_library *lr_open_memory(const void *data, size_t len, char *err, size_t err_len);
void lr_close(lr_library *lib);

/* icm = 1 for the ICM values, 0 for the chip-EV baseline. Returns 0 on success. */
int lr_query(const lr_library *lib, double hero_premium, double villain_premium, int position,
             int pot_type, double spr, const uint8_t flop[3], int icm, lr_values *out);

#ifdef __cplusplus
}
#endif

#endif
