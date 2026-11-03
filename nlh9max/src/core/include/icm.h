#ifndef ICMCORE_ICM_H
#define ICMCORE_ICM_H

#ifdef __cplusplus
extern "C" {
#endif

#define ICM_MAX_PLAYERS 9

/*
 * Malmuth-Harville ICM equity.
 *
 * stacks:   n chip counts (n = 1..9), each >= 0.
 * payouts:  npay prize amounts for 1st, 2nd, ... place; places beyond npay pay 0,
 *           payouts beyond n are ignored.
 * equity:   receives n prize-money equities.
 *
 * Players with a zero stack finish below every player with chips and share the
 * prizes for the places they occupy equally. Returns 0 on success, -1 on bad input.
 * Thread-safe; no allocation.
 */
int icm_equity(int n, const double *stacks, int npay, const double *payouts, double *equity);

#ifdef __cplusplus
}
#endif

#endif
