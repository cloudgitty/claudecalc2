# Improvement plan: progress (see ../NLH_IMPROVEMENT_PLAN.md)

## Phase 0 (item 1, interim)
- Started 2026-11-06: refit R from each build's 1,200 final solves, preflop re-solve (phase0.sh).

## Phase A (engineering), shared code in `nlh_improve/src`
- Item 5 (conserving payoffs): card removal only between the players who meet at a terminal;
  folded players as plain probabilities; 3-way consensus reach. Test (100bb tree, rake 0, R = 1,
  500 iterations): summed EV of the 8 seats +0.0659 (original) -> -0.0189 bb/hand (new default);
  a no-card-removal test mode gives exactly 0.0000. Original mode reproduces the old binary
  exactly (0.882 bb/100, +0.0659). RFI widths of the modes differ by <= 1 point.
- Item 6 (matrix-free terminals, un-pruned tree): no per-terminal matrices (memory O(1) per
  terminal). Pruning rules are config options (`cold_4bets`, `multiway_level`). Tree sizes:
  100bb 8,057 (pruned) / 60,343 (cold 4-bets) / 112,379 (+ 3-way 3-bet pots);
  200/300bb 16,395 / 74,736 / 133,022. Rough timing (100bb, shared cores): 0.8 s and 1.5 s per
  iteration for the two larger trees. Warm starts between rounds planned to keep the loop affordable.
- Item 4 (hand-type R regression): `src/rreg.py`; leave-one-spot-out RMSE of class R
  0.233 -> 0.229 (300bb n4) and 0.214 -> 0.211 (n5); pairs +0.11..0.15, suited +0.05..0.10.
  Small gain: most of the class-level error is 12-flop sampling noise (Phase C uses 30-50 flops).
- Items 7, 8 and checkpoints: the runner reads `postflop_max_raises` and
  `postflop_allin_extra_frac` from the config; `RIVER=1` solves flop to river. Mid-solve
  checkpoints every 600 s (`<prefix>.ckpt`), removed on completion. Kill-and-resume test: the
  resumed solve is bit-identical to an uninterrupted one (values, strategies, EVs).
