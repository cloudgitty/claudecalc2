#ifndef ICMCORE_RISKPREMIUM_H
#define ICMCORE_RISKPREMIUM_H

#ifdef __cplusplus
extern "C" {
#endif

enum { ROLE_SHORT = 0, ROLE_MID, ROLE_CHIP_LEADER, ROLE_RUNAWAY_LEADER };

/*
 * Risk premium of hero against villain: hero calls an all-in for the effective
 * stack min(hero, villain) with no other money in the pot; all other stacks stay
 * fixed. The ICM break-even equity is
 *     p* = ($fold - $lose) / ($win - $lose)
 * and the premium is p* - 0.5 (chip EV break-even is 50%). Result is a fraction
 * (0.08 = 8 percentage points). Returns 0 on success, -1 on bad input.
 */
int risk_premium(int n, const double *stacks, int npay, const double *payouts,
                 int hero, int villain, double *premium);

/* Required ICM equity p* for the same call. */
int risk_required_equity(int n, const double *stacks, int npay, const double *payouts,
                         int hero, int villain, double *required);

/* Stack role label (see the spec): short, mid, chip leader, runaway chip leader. */
int stack_role(int n, const double *stacks, int player, double big_blind);
const char *stack_role_name(int role);

#ifdef __cplusplus
}
#endif

#endif
