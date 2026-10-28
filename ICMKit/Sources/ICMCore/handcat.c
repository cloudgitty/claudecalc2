#include "handcat.h"
#include "cards.h"
#include "handeval.h"

int hand_draw_outs(const uint8_t hole[2], const uint8_t flop[3])
{
    uint8_t c[6] = { hole[0], hole[1], flop[0], flop[1], flop[2], 0 };
    int now = handeval_category(handeval(c, 5));
    int outs = 0;
    for (int x = 0; x < 52; x++) {
        int used = 0;
        for (int i = 0; i < 5; i++) used |= c[i] == x;
        if (used) continue;
        c[5] = (uint8_t)x;
        int cat = handeval_category(handeval(c, 6));
        /* a straight or flush (or straight flush) that the turn card completes */
        if ((cat == HAND_STRAIGHT || cat == HAND_FLUSH || cat == HAND_STRAIGHT_FLUSH) && cat != now) outs++;
    }
    return outs;
}

int hand_category(const uint8_t hole[2], const uint8_t flop[3])
{
    uint8_t c[5] = { hole[0], hole[1], flop[0], flop[1], flop[2] };
    for (int i = 0; i < 5; i++) {
        if (c[i] >= NUM_CARDS) return -1;
        for (int j = 0; j < i; j++) if (c[i] == c[j]) return -1;
    }
    int cat = handeval_category(handeval(c, 5));
    int h1 = card_rank(hole[0]), h2 = card_rank(hole[1]);
    int b[3] = { card_rank(flop[0]), card_rank(flop[1]), card_rank(flop[2]) };
    int top = b[0];
    for (int i = 1; i < 3; i++) if (b[i] > top) top = b[i];
    int board_pair = b[0] == b[1] || b[0] == b[2] || b[1] == b[2];
    int board_trips = b[0] == b[1] && b[1] == b[2];
    int hits1 = 0, hits2 = 0;
    for (int i = 0; i < 3; i++) { hits1 += b[i] == h1; hits2 += b[i] == h2; }
    int pocket = h1 == h2;

    if (cat >= HAND_STRAIGHT) {
        /* straights and flushes always use both hole cards on the flop; a full
           house or quads needs a hole card unless the board is trips */
        if (!board_trips || pocket || hits1 || hits2) return HC_TWO_PAIR_PLUS;
    }
    if (!board_trips) {
        if (pocket && hits1) return HC_TWO_PAIR_PLUS;                  /* set */
        if (board_pair && (hits1 || hits2)) {
            /* a hole card matches a board card */
            int pr = hits1 ? h1 : h2;
            int matches_pair = 0;
            for (int i = 0; i < 3; i++)
                for (int j = i + 1; j < 3; j++) matches_pair |= b[i] == b[j] && b[i] == pr;
            if (matches_pair) return HC_TWO_PAIR_PLUS;                 /* trips */
            if (hits1 && hits2 && h1 != h2) return HC_TWO_PAIR_PLUS;   /* cannot happen with 3 cards, kept for clarity */
            return pr == top ? HC_TOP_PAIR : HC_MID_BOTTOM_PAIR;       /* pairs the odd card */
        }
        if (!board_pair && hits1 && hits2) return HC_TWO_PAIR_PLUS;    /* two pair */
        if (!board_pair && (hits1 || hits2)) return (hits1 ? h1 : h2) == top ? HC_TOP_PAIR : HC_MID_BOTTOM_PAIR;
    }
    if (pocket) return h1 > top ? HC_TOP_PAIR : HC_MID_BOTTOM_PAIR;    /* over- or underpair */

    int outs = hand_draw_outs(hole, flop);
    if (outs >= 8) return HC_STRONG_DRAW;
    if (outs >= 1) return HC_WEAK_DRAW;
    return HC_AIR;
}

const char *hand_category_name(int category)
{
    static const char *names[HC_NUM_CATEGORIES] = {
        "sets and better / two pair", "top pair", "middle and bottom pair",
        "strong draw", "weak draw", "air"
    };
    return category >= 0 && category < HC_NUM_CATEGORIES ? names[category] : "?";
}
