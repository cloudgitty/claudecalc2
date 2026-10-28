/*
 * Test 1: hand evaluator.
 *   default : fast subset (all 2,598,960 five-card hands, a fixed sample of
 *             seven-card hands checked against brute force over 5-card subsets,
 *             and spot checks)
 *   --full  : enumerates all 133,784,560 seven-card hands and checks the
 *             standard category counts.
 */
#include "icmcore.h"
#include "check.h"
#include <string.h>
#include <time.h>

static const long long FULL7[HAND_NUM_CATEGORIES] = {
    23294460, 58627800, 31433400, 6461620, 6180020, 4047644, 3473184, 224848, 41584
};
static const long long FULL5[HAND_NUM_CATEGORIES] = {
    1302540, 1098240, 123552, 54912, 10200, 5108, 3744, 624, 40
};

static uint16_t eval_str(const char *s)
{
    uint8_t c[7];
    int n = cards_parse(s, c, 7);
    return handeval(c, n);
}

static void full_enumeration(void)
{
    long long counts[HAND_NUM_CATEGORIES] = { 0 };
    uint8_t c[7];
    clock_t t0 = clock();
    for (c[0] = 0; c[0] < 46; c[0]++)
    for (c[1] = c[0] + 1; c[1] < 47; c[1]++)
    for (c[2] = c[1] + 1; c[2] < 48; c[2]++)
    for (c[3] = c[2] + 1; c[3] < 49; c[3]++)
    for (c[4] = c[3] + 1; c[4] < 50; c[4]++)
    for (c[5] = c[4] + 1; c[5] < 51; c[5]++)
    for (c[6] = c[5] + 1; c[6] < 52; c[6]++)
        counts[handeval_category(handeval7(c))]++;
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    long long total = 0;
    for (int k = 0; k < HAND_NUM_CATEGORIES; k++) {
        printf("  %-16s %10lld (expected %10lld)\n", handeval_category_name(k), counts[k], FULL7[k]);
        CHECK(counts[k] == FULL7[k], "7-card %s: %lld vs %lld", handeval_category_name(k), counts[k], FULL7[k]);
        total += counts[k];
    }
    CHECK(total == 133784560LL, "total %lld", total);
    printf("full 7-card enumeration: %lld hands in %.1f s (%.1f M hands/s)\n", total, secs, total / secs / 1e6);
}

int main(int argc, char **argv)
{
    int full = argc > 1 && strcmp(argv[1], "--full") == 0;
    handeval_init();

    /* All five-card hands: standard category counts. */
    long long counts[HAND_NUM_CATEGORIES] = { 0 };
    uint8_t c[7];
    for (c[0] = 0; c[0] < 48; c[0]++)
    for (c[1] = c[0] + 1; c[1] < 49; c[1]++)
    for (c[2] = c[1] + 1; c[2] < 50; c[2]++)
    for (c[3] = c[2] + 1; c[3] < 51; c[3]++)
    for (c[4] = c[3] + 1; c[4] < 52; c[4]++)
        counts[handeval_category(handeval(c, 5))]++;
    for (int k = 0; k < HAND_NUM_CATEGORIES; k++)
        CHECK(counts[k] == FULL5[k], "5-card %s: %lld vs %lld", handeval_category_name(k), counts[k], FULL5[k]);

    /* Seven-card sample: equals the best of its 21 five-card subsets; 6-card likewise. */
    unsigned rng = 2024;
    int samples = 200000;
    for (int t = 0; t < samples; t++) {
        uint8_t h[7];
        int n = 0;
        while (n < 7) {
            rng = rng * 1664525u + 1013904223u;
            uint8_t x = (uint8_t)((rng >> 10) % 52);
            int dup = 0;
            for (int i = 0; i < n; i++) dup |= h[i] == x;
            if (!dup) h[n++] = x;
        }
        uint16_t best = 0;
        for (int a = 0; a < 7; a++)
            for (int b = a + 1; b < 7; b++) {
                uint8_t f[5];
                int k = 0;
                for (int i = 0; i < 7; i++) if (i != a && i != b) f[k++] = h[i];
                uint16_t v = handeval(f, 5);
                if (v > best) best = v;
            }
        uint16_t v7 = handeval7(h);
        CHECK(v7 == best, "sample %d: 7-card %u vs best subset %u", t, v7, best);
        uint16_t best6 = 0;
        for (int a = 0; a < 6; a++) {
            uint8_t f[5];
            int k = 0;
            for (int i = 0; i < 6; i++) if (i != a) f[k++] = h[i];
            uint16_t v = handeval(f, 5);
            if (v > best6) best6 = v;
        }
        CHECK(handeval(h, 6) == best6, "sample %d: 6-card mismatch", t);
        if (check_failures > 10) break;
    }

    /* Spot checks of ordering. */
    CHECK(eval_str("AsKsQsJsTs2c3d") == 7462, "royal flush is the top class");
    CHECK(eval_str("5s4s3s2sAs") > eval_str("KdKcKhKsQc"), "steel wheel beats quads");
    CHECK(eval_str("5d4s3c2hAs") < eval_str("6d5s4c3h2s"), "wheel is the lowest straight");
    CHECK(handeval_category(eval_str("5d4s3c2hAs9c9d")) == HAND_STRAIGHT, "wheel + pair is a straight");
    CHECK(eval_str("AhAdKcKsQh") > eval_str("AhAdKcKsJh"), "two pair kicker");
    CHECK(eval_str("AhAdAcKsKh2c2d") == eval_str("AhAdAcKsKh"), "full house ignores extra pair");
    CHECK(eval_str("7c7d7h2s2c2d3h") == eval_str("7c7d7h2s2c"), "two trips make a full house");
    CHECK(eval_str("7c8c9cTcJcQdKd") == eval_str("7c8c9cTcJc"), "straight flush with higher straight");
    CHECK(eval_str("2c3d4h5s7c8d9h") < eval_str("2c3d4h5s7c8dTh"), "high card kicker");
    CHECK(handeval_category(eval_str("2c3d4h5s7c8d9h")) == HAND_HIGH_CARD, "7-card high card");
    CHECK(eval_str("AsAcAdAhKs") > eval_str("AsAcAdAhQs"), "quads kicker");

    printf("handeval fast subset: 2,598,960 five-card hands + %d seven-card samples\n", samples);
    if (full) full_enumeration();
    return check_finish(full ? "test_handeval --full" : "test_handeval");
}
