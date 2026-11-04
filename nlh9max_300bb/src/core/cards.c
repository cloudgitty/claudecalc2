#include "cards.h"

static const char RANKS[] = "23456789TJQKA";
static const char SUITS[] = "cdhs";

static int rank_of_char(char ch)
{
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    for (int i = 0; i < 13; i++)
        if (RANKS[i] == ch) return i;
    return -1;
}

static int suit_of_char(char ch)
{
    if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    for (int i = 0; i < 4; i++)
        if (SUITS[i] == ch) return i;
    return -1;
}

int card_parse(const char *s)
{
    if (!s || !s[0] || !s[1]) return CARD_NONE;
    int r = rank_of_char(s[0]), u = suit_of_char(s[1]);
    if (r < 0 || u < 0) return CARD_NONE;
    return make_card(r, u);
}

void card_format(int card, char *out)
{
    if (card < 0 || card >= NUM_CARDS) {
        out[0] = '?'; out[1] = '?'; out[2] = 0;
        return;
    }
    out[0] = RANKS[card_rank(card)];
    out[1] = SUITS[card_suit(card)];
    out[2] = 0;
}

int cards_parse(const char *s, uint8_t *out, int max)
{
    int n = 0;
    while (*s) {
        if (*s == ' ' || *s == ',') { s++; continue; }
        int c = card_parse(s);
        if (c == CARD_NONE || n >= max) return -1;
        for (int i = 0; i < n; i++)
            if (out[i] == c) return -1;
        out[n++] = (uint8_t)c;
        s += 2;
    }
    return n;
}

void combo_cards(int index, int *hi, int *lo)
{
    /* hi is the largest h with h * (h - 1) / 2 <= index. */
    int h = 1;
    while ((h + 1) * h / 2 <= index) h++;
    *hi = h;
    *lo = index - h * (h - 1) / 2;
}
