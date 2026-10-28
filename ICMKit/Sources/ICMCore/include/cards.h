#ifndef ICMCORE_CARDS_H
#define ICMCORE_CARDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A card is rank * 4 + suit, rank 0..12 = 2..A, suit 0..3 = c, d, h, s. */
#define CARD_NONE 255
#define NUM_CARDS 52
#define NUM_COMBOS 1326

static inline int card_rank(int c) { return c >> 2; }
static inline int card_suit(int c) { return c & 3; }
static inline int make_card(int rank, int suit) { return rank * 4 + suit; }

/* Parses "As", "Td", "2c" (case-insensitive rank, lower-case suit). Returns CARD_NONE on error. */
int card_parse(const char *s);
/* Writes two characters and a terminating NUL into out (at least 3 bytes). */
void card_format(int card, char *out);
/* Parses a run of cards such as "Ks7d2c" or "Ks 7d 2c". Returns the count, or -1 on error. */
int cards_parse(const char *s, uint8_t *out, int max);

/* Hole-card combos are indexed 0..1325; the two cards satisfy hi > lo. */
static inline int combo_index(int a, int b)
{
    int hi = a > b ? a : b, lo = a > b ? b : a;
    return hi * (hi - 1) / 2 + lo;
}
void combo_cards(int index, int *hi, int *lo);

#ifdef __cplusplus
}
#endif

#endif
