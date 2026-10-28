/* Flop classifier: every representative flop lands in its class; counts over all flops. */
#include "icmcore.h"
#include "check.h"

int main(void)
{
    for (int cls = 0; cls < FLOP_NUM_CLASSES; cls++)
        for (int k = 0; k < 3; k++) {
            uint8_t c[3];
            const char *s = flop_representative(cls, k);
            CHECK(cards_parse(s, c, 3) == 3, "parse %s", s);
            int got = flop_classify(c);
            CHECK(got == cls, "%s classified as %s, listed as %s", s, flop_class_name(got), flop_class_name(cls));
        }

    long counts[FLOP_NUM_CLASSES] = { 0 };
    uint8_t c[3];
    for (c[0] = 0; c[0] < 52; c[0]++)
        for (c[1] = c[0] + 1; c[1] < 52; c[1]++)
            for (c[2] = c[1] + 1; c[2] < 52; c[2]++)
                counts[flop_classify(c)]++;
    long total = 0;
    for (int k = 0; k < FLOP_NUM_CLASSES; k++) {
        printf("  %-10s %5ld flops (%.1f%%)\n", flop_class_name(k), counts[k], 100.0 * counts[k] / 22100);
        total += counts[k];
    }
    CHECK(total == 22100, "total %ld", total);
    CHECK(counts[FLOP_PAIRED] == 3796, "paired flops %ld", counts[FLOP_PAIRED]);

    uint8_t bad[3] = { 1, 1, 2 };
    CHECK(flop_classify(bad) == -1, "duplicate cards accepted");
    return check_finish("test_flopclass");
}
