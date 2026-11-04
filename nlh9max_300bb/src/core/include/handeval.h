#ifndef ICMCORE_HANDEVAL_H
#define ICMCORE_HANDEVAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HAND_HIGH_CARD = 0,
    HAND_PAIR,
    HAND_TWO_PAIR,
    HAND_TRIPS,
    HAND_STRAIGHT,
    HAND_FLUSH,
    HAND_FULL_HOUSE,
    HAND_QUADS,
    HAND_STRAIGHT_FLUSH,
    HAND_NUM_CATEGORIES
};

/*
 * Hand strength is a number 1..7462 (higher is better); equal numbers tie.
 * The tables are built on first use (thread-safe, once per process) and are
 * read-only afterwards. handeval_init() may be called up front to avoid the
 * one-off cost (a few milliseconds) inside a timed section.
 */
void handeval_init(void);

/* Best five-card strength of 5, 6 or 7 distinct cards (see cards.h encoding). */
uint16_t handeval(const uint8_t *cards, int n);
uint16_t handeval7(const uint8_t *cards);

int handeval_category(uint16_t strength);
const char *handeval_category_name(int category);

#ifdef __cplusplus
}
#endif

#endif
