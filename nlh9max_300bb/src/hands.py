"""Hand classes, combos and card helpers shared by the Python tools.

Cards: rank * 4 + suit, rank 0..12 = 2..A, suit 0..3 = c, d, h, s (same as src/core/cards.h).
Combos: index hi * (hi - 1) / 2 + lo for cards hi > lo, 0..1325 (combos.json lists them).
Hand classes: grid index i * 13 + j, i, j = 0..12 for ranks A..2; i == j pair,
i < j suited, i > j offsuit. Labels "AA", "AKs", "AKo", ...
"""
RANKS = "23456789TJQKA"
SUITS = "cdhs"


def card_str(c):
    return RANKS[c >> 2] + SUITS[c & 3]


def combo_cards(i):
    hi = 1
    while hi * (hi + 1) // 2 <= i:
        hi += 1
    lo = i - hi * (hi - 1) // 2
    return hi, lo


COMBOS = [combo_cards(i) for i in range(1326)]
COMBO_STR = [card_str(a) + card_str(b) for a, b in COMBOS]


def class_of(a, b):
    r1, r2 = 12 - (a >> 2), 12 - (b >> 2)
    if r1 > r2:
        r1, r2 = r2, r1
    if r1 == r2:
        return r1 * 13 + r1
    if (a & 3) == (b & 3):
        return r1 * 13 + r2
    return r2 * 13 + r1


def class_label(k):
    i, j = divmod(k, 13)
    hi, lo = "AKQJT98765432"[min(i, j)], "AKQJT98765432"[max(i, j)]
    if i == j:
        return hi + hi
    return hi + lo + ("s" if i < j else "o")


LABELS = [class_label(k) for k in range(169)]
COMBO_CLASS = [class_of(a, b) for a, b in COMBOS]
CLASS_COMBOS = [[c for c in range(1326) if COMBO_CLASS[c] == k] for k in range(169)]
NCOMBOS = [len(x) for x in CLASS_COMBOS]
