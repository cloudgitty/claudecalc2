/* Hand categories for flop summaries. */
#include "icmcore.h"
#include "check.h"
#include <string.h>

static int cat(const char *hole, const char *flop)
{
    uint8_t h[2], f[3];
    cards_parse(hole, h, 2);
    cards_parse(flop, f, 3);
    return hand_category(h, f);
}

static int outs(const char *hole, const char *flop)
{
    uint8_t h[2], f[3];
    cards_parse(hole, h, 2);
    cards_parse(flop, f, 3);
    return hand_draw_outs(h, f);
}

#define EXPECT(hole, flop, want) CHECK(cat(hole, flop) == (want), "%s on %s: %s, want %s", hole, flop, \
                                       hand_category_name(cat(hole, flop)), hand_category_name(want))

int main(void)
{
    /* made hands */
    EXPECT("7h7c", "Ks7d2c", HC_TWO_PAIR_PLUS);   /* set */
    EXPECT("Kh7h", "Ks7d2c", HC_TWO_PAIR_PLUS);   /* two pair */
    EXPECT("6h5h", "9s8d7c", HC_TWO_PAIR_PLUS);   /* straight */
    EXPECT("Ah2h", "Kh9h4h", HC_TWO_PAIR_PLUS);   /* flush */
    EXPECT("Kh4h", "KsKd4c", HC_TWO_PAIR_PLUS);   /* full house */
    EXPECT("Kh9c", "KsKd4c", HC_TWO_PAIR_PLUS);   /* trips */
    EXPECT("AhKh", "Ks7d2c", HC_TOP_PAIR);
    EXPECT("AhAc", "Ks7d2c", HC_TOP_PAIR);         /* overpair */
    EXPECT("Ah7h", "Ks7d2c", HC_MID_BOTTOM_PAIR);
    EXPECT("Ah2h", "Ks7d2c", HC_MID_BOTTOM_PAIR);
    EXPECT("QhQc", "Ks7d2c", HC_MID_BOTTOM_PAIR);  /* underpair */
    EXPECT("Ah4h", "KsKd4c", HC_MID_BOTTOM_PAIR);  /* pairs the odd card below the board pair */
    EXPECT("Ah8h", "8s8d3h", HC_TWO_PAIR_PLUS);    /* trips on a paired board */
    EXPECT("AhAc", "8s8d3h", HC_TOP_PAIR);         /* overpair on a paired board */
    EXPECT("Ah5h", "5s5dAc", HC_TWO_PAIR_PLUS);    /* full house */
    EXPECT("AhKd", "5s5dAc", HC_TOP_PAIR);         /* pairs the odd (top) card */
    /* draws and air */
    CHECK(outs("Ah2h", "Kh9h4c") == 9, "nut flush draw has 9 outs (%d)", outs("Ah2h", "Kh9h4c"));
    CHECK(outs("Th9c", "Js8d2h") == 8, "open-ender has 8 outs (%d)", outs("Th9c", "Js8d2h"));
    CHECK(outs("Th7c", "Js8d2h") == 4, "gutshot has 4 outs (%d)", outs("Th7c", "Js8d2h"));
    EXPECT("Ah2h", "Kh9h4c", HC_STRONG_DRAW);
    EXPECT("Th9c", "Js8d2h", HC_STRONG_DRAW);
    EXPECT("Th7c", "Js8d2h", HC_WEAK_DRAW);
    EXPECT("Ah4c", "Ks3d2c", HC_WEAK_DRAW);         /* wheel gutshot */
    EXPECT("QhJc", "Ks7d2c", HC_AIR);
    EXPECT("Qh9h", "KsKd4c", HC_AIR);               /* board pair alone is not a pair */
    EXPECT("JhTh", "Qh9h4c", HC_STRONG_DRAW);       /* flush draw + open-ender (15 outs) */

    /* every combo on every representative flop gets a valid category */
    for (int cls = 0; cls < FLOP_NUM_CLASSES; cls++)
        for (int k = 0; k < 3; k++) {
            uint8_t f[3];
            cards_parse(flop_representative(cls, k), f, 3);
            int count[HC_NUM_CATEGORIES] = { 0 };
            for (int i = 0; i < NUM_COMBOS; i++) {
                int a, b;
                combo_cards(i, &a, &b);
                if (a == f[0] || a == f[1] || a == f[2] || b == f[0] || b == f[1] || b == f[2]) continue;
                uint8_t h[2] = { (uint8_t)a, (uint8_t)b };
                int c = hand_category(h, f);
                CHECK(c >= 0 && c < HC_NUM_CATEGORIES, "bad category");
                if (c >= 0) count[c]++;
            }
            if (cls == 0 && k == 0) {
                printf("categories of all 1176 combos on %s:", flop_representative(cls, k));
                for (int c = 0; c < HC_NUM_CATEGORIES; c++) printf(" %s %d,", hand_category_name(c), count[c]);
                printf("\n");
            }
        }
    return check_finish("test_handcat");
}
