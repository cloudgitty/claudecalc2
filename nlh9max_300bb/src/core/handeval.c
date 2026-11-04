#include "handeval.h"
#include "cards.h"
#include <pthread.h>
#include <stdlib.h>

/*
 * Two lookup tables:
 *  - flush_table[mask]: strength of the best flush or straight flush that a
 *    13-bit set of same-suit ranks contains (masks with 5+ bits).
 *  - multiset tables: strength of the best non-flush hand for every multiset of
 *    5, 6 or 7 ranks (at most 4 of each), addressed by a lexicographic rank of
 *    the per-rank counts.
 * With 7 cards a flush, when present, always beats the best non-flush hand
 * (no full house or quads fits beside five cards of one suit), so the result is
 * the flush entry if any suit has 5+ cards and the multiset entry otherwise.
 */

#define NUM_CLASSES 7462
#define MAX_KEYS 7462

static uint16_t flush_table[8192];
static uint16_t ms_table5[6175], ms_table6[18395], ms_table7[49205];
static uint16_t *const ms_table[8] = { 0, 0, 0, 0, 0, ms_table5, ms_table6, ms_table7 };
static const uint32_t ms_size[8] = { 0, 0, 0, 0, 0, 6175, 18395, 49205 };
static uint32_t rank_offset[13][8][5]; /* O[r][k][c] */
static uint16_t category_start[HAND_NUM_CATEGORIES + 1];
static pthread_once_t init_once = PTHREAD_ONCE_INIT;

/* Comparable key: category << 20 | five 4-bit rank fields, most significant first. */
static uint32_t make_key(int cat, const int *r, int nr)
{
    uint32_t k = (uint32_t)cat << 20;
    for (int i = 0; i < 5; i++) k |= (uint32_t)(i < nr ? r[i] : 0) << (16 - 4 * i);
    return k;
}

/* Highest straight in a 13-bit rank mask: returns top rank (3 = five-high) or -1. */
static int straight_top(unsigned mask)
{
    for (int top = 12; top >= 4; top--) {
        unsigned need = 0x1fu << (top - 4);
        if ((mask & need) == need) return top;
    }
    if ((mask & 0x100fu) == 0x100fu) return 3;   /* A-2-3-4-5 */
    return -1;
}

static uint32_t flush_key(unsigned mask)
{
    int st = straight_top(mask);
    if (st >= 0) { int r[1] = { st }; return make_key(HAND_STRAIGHT_FLUSH, r, 1); }
    int r[5], n = 0;
    for (int i = 12; i >= 0 && n < 5; i--)
        if (mask & (1u << i)) r[n++] = i;
    return make_key(HAND_FLUSH, r, 5);
}

/* Best non-flush key for per-rank counts (sum 5..7, each <= 4). */
static uint32_t multiset_key(const int *cnt)
{
    unsigned present = 0;
    for (int i = 0; i < 13; i++) if (cnt[i]) present |= 1u << i;

    int quad = -1, trips[3], nt = 0, pairs[4], np = 0, singles[7], ns = 0;
    for (int i = 12; i >= 0; i--) {
        if (cnt[i] == 4 && quad < 0) quad = i;
        else if (cnt[i] == 4) trips[nt++] = i;  /* cannot happen with <= 7 cards */
        else if (cnt[i] == 3) trips[nt++] = i;
        else if (cnt[i] == 2) pairs[np++] = i;
        else if (cnt[i] == 1) singles[ns++] = i;
    }
    if (quad >= 0) {
        int kick = -1;
        for (int i = 12; i >= 0; i--) if (i != quad && cnt[i]) { kick = i; break; }
        int r[2] = { quad, kick };
        return make_key(HAND_QUADS, r, 2);
    }
    if (nt >= 2 || (nt == 1 && np >= 1)) {
        int pair = nt >= 2 ? trips[1] : pairs[0];
        if (nt >= 2 && np >= 1 && pairs[0] > pair) pair = pairs[0];
        int r[2] = { trips[0], pair };
        return make_key(HAND_FULL_HOUSE, r, 2);
    }
    int st = straight_top(present);
    if (st >= 0) { int r[1] = { st }; return make_key(HAND_STRAIGHT, r, 1); }
    if (nt == 1) {
        int r[3] = { trips[0], -1, -1 }, k = 1;
        for (int i = 12; i >= 0 && k < 3; i--) if (i != trips[0] && cnt[i]) r[k++] = i;
        return make_key(HAND_TRIPS, r, 3);
    }
    if (np >= 2) {
        int r[3] = { pairs[0], pairs[1], -1 };
        for (int i = 12; i >= 0; i--) if (i != pairs[0] && i != pairs[1] && cnt[i]) { r[2] = i; break; }
        return make_key(HAND_TWO_PAIR, r, 3);
    }
    if (np == 1) {
        int r[4] = { pairs[0] }, k = 1;
        for (int i = 0; i < ns && k < 4; i++) r[k++] = singles[i];
        return make_key(HAND_PAIR, r, 4);
    }
    return make_key(HAND_HIGH_CARD, singles, 5);
}

static uint32_t sorted_keys[MAX_KEYS];
static int num_keys;

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static uint16_t strength_of_key(uint32_t key)
{
    int lo = 0, hi = num_keys - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (sorted_keys[mid] == key) return (uint16_t)(mid + 1);
        if (sorted_keys[mid] < key) lo = mid + 1; else hi = mid - 1;
    }
    abort();   /* every key is collected during init */
}

/* Calls fn for every count vector over ranks r..12 summing to k (lexicographic order). */
typedef void (*ms_fn)(const int *cnt, void *ctx);
static void each_multiset(int *cnt, int r, int k, ms_fn fn, void *ctx)
{
    if (r == 13) { if (k == 0) fn(cnt, ctx); return; }
    for (int c = 0; c <= 4 && c <= k; c++) {
        cnt[r] = c;
        each_multiset(cnt, r + 1, k - c, fn, ctx);
    }
    cnt[r] = 0;
}

static uint32_t key_buffer[20000];
static int key_count;

static void collect_key(const int *cnt, void *ctx)
{
    (void)ctx;
    key_buffer[key_count++] = multiset_key(cnt);
}

static uint32_t ms_index(const int *cnt)
{
    uint32_t idx = 0;
    int k = 0;
    for (int r = 0; r < 13; r++) k += cnt[r];
    for (int r = 0; r < 13; r++) {
        idx += rank_offset[r][k][cnt[r]];
        k -= cnt[r];
    }
    return idx;
}

static void fill_entry(const int *cnt, void *ctx)
{
    uint16_t *table = ctx;
    table[ms_index(cnt)] = strength_of_key(multiset_key(cnt));
}

static void build_tables(void)
{
    /* N[r][k]: count vectors over ranks r..12 summing to k. */
    uint32_t N[14][8] = { { 0 } };
    N[13][0] = 1;
    for (int r = 12; r >= 0; r--)
        for (int k = 0; k <= 7; k++)
            for (int c = 0; c <= 4 && c <= k; c++) N[r][k] += N[r + 1][k - c];
    for (int r = 0; r < 13; r++)
        for (int k = 0; k <= 7; k++) {
            uint32_t acc = 0;
            for (int c = 0; c <= 4; c++) {
                rank_offset[r][k][c] = acc;
                if (c <= k) acc += N[r + 1][k - c];
            }
        }

    /* Collect every distinct five-card class: rank multisets of 5 plus flushes. */
    int cnt[13] = { 0 };
    key_count = 0;
    each_multiset(cnt, 0, 5, collect_key, NULL);
    for (unsigned m = 0; m < 8192; m++)
        if (__builtin_popcount(m) == 5) key_buffer[key_count++] = flush_key(m);
    qsort(key_buffer, (size_t)key_count, sizeof(uint32_t), cmp_u32);
    num_keys = 0;
    for (int i = 0; i < key_count; i++)
        if (num_keys == 0 || sorted_keys[num_keys - 1] != key_buffer[i]) {
            if (num_keys >= MAX_KEYS) abort();
            sorted_keys[num_keys++] = key_buffer[i];
        }
    if (num_keys != NUM_CLASSES) abort();

    for (int c = 0; c <= HAND_NUM_CATEGORIES; c++) category_start[c] = NUM_CLASSES + 1;
    for (int i = num_keys - 1; i >= 0; i--) category_start[sorted_keys[i] >> 20] = (uint16_t)(i + 1);

    for (unsigned m = 0; m < 8192; m++)
        flush_table[m] = __builtin_popcount(m) >= 5 ? strength_of_key(flush_key(m)) : 0;

    for (int n = 5; n <= 7; n++) {
        if (N[0][n] != ms_size[n]) abort();
        each_multiset(cnt, 0, n, fill_entry, ms_table[n]);
    }
}

void handeval_init(void)
{
    pthread_once(&init_once, build_tables);
}

static inline uint16_t eval_counts(const int *cnt, const unsigned *suit_mask, int n)
{
    for (int s = 0; s < 4; s++)
        if (__builtin_popcount(suit_mask[s]) >= 5) return flush_table[suit_mask[s]];
    return ms_table[n][ms_index(cnt)];
}

uint16_t handeval(const uint8_t *cards, int n)
{
    handeval_init();
    int cnt[13] = { 0 };
    unsigned sm[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < n; i++) {
        int c = cards[i];
        cnt[c >> 2]++;
        sm[c & 3] |= 1u << (c >> 2);
    }
    return eval_counts(cnt, sm, n);
}

uint16_t handeval7(const uint8_t *cards)
{
    return handeval(cards, 7);
}

int handeval_category(uint16_t strength)
{
    handeval_init();
    int c = HAND_STRAIGHT_FLUSH;
    while (c > 0 && strength < category_start[c]) c--;
    return c;
}

const char *handeval_category_name(int category)
{
    static const char *names[HAND_NUM_CATEGORIES] = {
        "high card", "one pair", "two pair", "three of a kind", "straight",
        "flush", "full house", "four of a kind", "straight flush"
    };
    return category >= 0 && category < HAND_NUM_CATEGORIES ? names[category] : "?";
}
