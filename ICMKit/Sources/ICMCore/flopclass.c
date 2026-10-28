#include "flopclass.h"
#include "cards.h"

int flop_classify(const uint8_t cards[3])
{
    for (int i = 0; i < 3; i++)
        if (cards[i] >= NUM_CARDS) return -1;
    if (cards[0] == cards[1] || cards[0] == cards[2] || cards[1] == cards[2]) return -1;

    int r0 = card_rank(cards[0]), r1 = card_rank(cards[1]), r2 = card_rank(cards[2]);
    int s0 = card_suit(cards[0]), s1 = card_suit(cards[1]), s2 = card_suit(cards[2]);
    int hi = r0, lo = r0;
    if (r1 > hi) hi = r1;
    if (r2 > hi) hi = r2;
    if (r1 < lo) lo = r1;
    if (r2 < lo) lo = r2;
    int suits = 1 + (s1 != s0) + (s2 != s0 && s2 != s1);

    if (r0 == r1 || r0 == r2 || r1 == r2) return FLOP_PAIRED;
    if (suits == 1) return FLOP_MONOTONE;
    if (hi - lo <= 4) return FLOP_CONNECTED;
    if (suits == 2) return FLOP_TWO_TONE;
    if (hi >= 8) return FLOP_DRY_HIGH;   /* rank 8 = T */
    return FLOP_DRY_LOW;
}

const char *flop_class_name(int cls)
{
    static const char *names[FLOP_NUM_CLASSES] = {
        "dry high", "dry low", "paired", "two-tone", "monotone", "connected"
    };
    return cls >= 0 && cls < FLOP_NUM_CLASSES ? names[cls] : "?";
}

const char *flop_representative(int cls, int k)
{
    static const char *reps[FLOP_NUM_CLASSES][3] = {
        { "Ks7d2c", "Ah8d3c", "Qs6d2h" },
        { "9s5d2c", "8h4d3c", "7s3d2h" },
        { "KsKd4c", "8s8d3h", "5s5dAc" },
        { "AsJs5d", "Qh9h4c", "Td6d2s" },
        { "Ks9s4s", "Jh7h3h", "AdTd5d" },
        { "9s8d7c", "JsTd8h", "6s5d4h" },
    };
    if (cls < 0 || cls >= FLOP_NUM_CLASSES || k < 0 || k > 2) return 0;
    return reps[cls][k];
}
