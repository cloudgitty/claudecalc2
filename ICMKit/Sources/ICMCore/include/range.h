#ifndef ICMCORE_RANGE_H
#define ICMCORE_RANGE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parses standard range notation into 1326 combo weights (cards.h combo index).
 * Items are comma separated, each optionally followed by ":weight" (0..1):
 *   AA  AKs  AKo  AK (suited and offsuit)  TT+  ATs+  KTo+  A9+  22-66  A2s-A5s
 *   AsKs (one combo)
 * Later items overwrite earlier ones. Returns 0 on success, -1 on a syntax error
 * (out is then unspecified).
 */
int range_parse(const char *text, float *out);

/* Number of combos with weight > 0 and the weighted combo count. */
int range_count(const float *w, double *weighted);

#ifdef __cplusplus
}
#endif

#endif
