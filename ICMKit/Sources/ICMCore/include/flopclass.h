#ifndef ICMCORE_FLOPCLASS_H
#define ICMCORE_FLOPCLASS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FLOP_DRY_HIGH = 0,
    FLOP_DRY_LOW,
    FLOP_PAIRED,
    FLOP_TWO_TONE,
    FLOP_MONOTONE,
    FLOP_CONNECTED,
    FLOP_NUM_CLASSES
};

/*
 * First rule that matches: paired -> monotone -> connected (no pair, highest
 * minus lowest rank <= 4, ace counted high) -> two-tone -> dry high (rainbow,
 * highest card T or above) -> dry low. Returns -1 if the cards are invalid.
 */
int flop_classify(const uint8_t cards[3]);
const char *flop_class_name(int cls);
/* Representative flop k (0..2) of a class, e.g. "Ks7d2c". */
const char *flop_representative(int cls, int k);

#ifdef __cplusplus
}
#endif

#endif
