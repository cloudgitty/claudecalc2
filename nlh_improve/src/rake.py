"""Rake rule (config.json): rake_pct of the pot, capped at rake_cap_bb, no flop no drop.

The pot raked is the matched pot when the hand ends: uncalled bets go back to the bettor
before the rake is taken. A hand that ends before the flop is not raked; a preflop all-in
is raked because the flop is dealt.
"""


def rake(pot, saw_flop, pct=0.10, cap=5.0, no_flop_no_drop=True):
    if no_flop_no_drop and not saw_flop:
        return 0.0
    return min(pct * pot, cap)


def matched_pot(start_pot, inv_a, inv_b):
    """Pot after returning the uncalled part of the larger investment."""
    return start_pot + 2.0 * min(inv_a, inv_b)
