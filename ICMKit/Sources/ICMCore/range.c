#include "range.h"
#include "cards.h"
#include <stdlib.h>
#include <string.h>

static int rank_char(char c)
{
    static const char R[] = "23456789TJQKA";
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (int i = 0; i < 13; i++) if (R[i] == c) return i;
    return -1;
}

/* kind: 0 = pair, 1 = suited, 2 = offsuit, 3 = both */
static void set_class(float *out, int r1, int r2, int kind, float wt)
{
    for (int s1 = 0; s1 < 4; s1++)
        for (int s2 = 0; s2 < 4; s2++) {
            int a = make_card(r1, s1), b = make_card(r2, s2);
            if (a == b) continue;
            if (r1 == r2) { if (s1 >= s2) continue; }
            else if (kind == 1 && s1 != s2) continue;
            else if (kind == 2 && s1 == s2) continue;
            out[combo_index(a, b)] = wt;
        }
}

/* Parses "AK", "AKs", "AKo", "TT" at p into ranks and kind. Returns chars used or 0. */
static int parse_class(const char *p, int *r1, int *r2, int *kind)
{
    int a = rank_char(p[0]), b = a >= 0 ? rank_char(p[1]) : -1;
    if (a < 0 || b < 0) return 0;
    if (a < b) { int t = a; a = b; b = t; }
    *r1 = a; *r2 = b;
    if (a == b) { *kind = 0; return 2; }
    if (p[2] == 's') { *kind = 1; return 3; }
    if (p[2] == 'o') { *kind = 2; return 3; }
    *kind = 3;
    return 2;
}

static int parse_item(const char *s, size_t len, float *out)
{
    char buf[32];
    if (len == 0) return 0;
    if (len >= sizeof(buf)) return -1;
    memcpy(buf, s, len);
    buf[len] = 0;

    float wt = 1.0f;
    char *colon = strchr(buf, ':');
    if (colon) {
        *colon = 0;
        char *end;
        wt = strtof(colon + 1, &end);
        if (end == colon + 1 || *end || !(wt >= 0.0f) || wt > 1.0f) return -1;
    }
    size_t n = strlen(buf);

    /* single combo such as AsKs */
    if (n == 4) {
        int a = card_parse(buf), b = card_parse(buf + 2);
        if (a != CARD_NONE && b != CARD_NONE && a != b && buf[1] >= 'a') {
            out[combo_index(a, b)] = wt;
            return 0;
        }
    }

    int r1, r2, kind;
    int used = parse_class(buf, &r1, &r2, &kind);
    if (!used) return -1;
    const char *rest = buf + used;
    if (*rest == 0) { set_class(out, r1, r2, kind, wt); return 0; }
    if (rest[0] == '+' && rest[1] == 0) {
        if (kind == 0) { for (int r = r1; r < 13; r++) set_class(out, r, r, 0, wt); }
        else { for (int r = r2; r < r1; r++) set_class(out, r1, r, kind, wt); }
        return 0;
    }
    if (rest[0] == '-') {
        int q1, q2, k2;
        int u2 = parse_class(rest + 1, &q1, &q2, &k2);
        if (!u2 || rest[1 + u2] != 0 || k2 != kind) return -1;
        if (kind == 0) {
            int lo = r1 < q1 ? r1 : q1, hi = r1 < q1 ? q1 : r1;
            for (int r = lo; r <= hi; r++) set_class(out, r, r, 0, wt);
            return 0;
        }
        if (q1 != r1) return -1;
        int lo = r2 < q2 ? r2 : q2, hi = r2 < q2 ? q2 : r2;
        for (int r = lo; r <= hi; r++) set_class(out, r1, r, kind, wt);
        return 0;
    }
    return -1;
}

int range_parse(const char *text, float *out)
{
    for (int i = 0; i < NUM_COMBOS; i++) out[i] = 0.0f;
    const char *p = text;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t') p++;
        const char *start = p;
        while (*p && *p != ',' && *p != ' ' && *p != '\n' && *p != '\t') p++;
        if (parse_item(start, (size_t)(p - start), out)) return -1;
    }
    return 0;
}

int range_count(const float *w, double *weighted)
{
    int n = 0;
    double s = 0.0;
    for (int i = 0; i < NUM_COMBOS; i++)
        if (w[i] > 0.0f) { n++; s += w[i]; }
    if (weighted) *weighted = s;
    return n;
}
