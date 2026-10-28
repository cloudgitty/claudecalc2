#ifndef ICMCORE_HANDCAT_H
#define ICMCORE_HANDCAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The spec's hand categories for flop summaries (strongest first). */
enum {
    HC_TWO_PAIR_PLUS = 0,   /* sets and better / two pair (using the hole cards) */
    HC_TOP_PAIR,            /* top pair or an overpair */
    HC_MID_BOTTOM_PAIR,     /* middle or bottom pair, or an underpair */
    HC_STRONG_DRAW,         /* no pair, 8 or more outs to a straight or flush */
    HC_WEAK_DRAW,           /* no pair, 1 to 7 such outs */
    HC_AIR,
    HC_NUM_CATEGORIES
};

/*
 * Category of two hole cards on a 3-card flop. Made hands come first: a pair or
 * better that uses a hole card is never counted as a draw. A board pair alone
 * does not count as the player's pair. Outs are turn cards that give a straight
 * or better (straight, flush, ...) with the hole cards. Returns -1 on bad input.
 */
int hand_category(const uint8_t hole[2], const uint8_t flop[3]);

/* Number of one-card outs to a straight or flush (see above). */
int hand_draw_outs(const uint8_t hole[2], const uint8_t flop[3]);

const char *hand_category_name(int category);

#ifdef __cplusplus
}
#endif

#endif
