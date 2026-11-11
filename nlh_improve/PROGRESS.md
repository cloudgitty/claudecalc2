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
- Preflop warm start (item 2): regrets from the previous round, strategy sums cleared, iteration
  count continued (restarting it discards the regrets: 4.5 vs 1.0 bb/100). Same-payoff test:
  0.19 bb/100 after 50 warm iterations (cold: ~10 after 100).
- River correction (item 8): `src/river_corr.py` (river R / turn-end R per spot, side, class,
  shrunk to buckets; group averages for unsolved pairs), applied by `rmodel.py` via RIVER_CORR.
- Phase C pipeline: `src/pipeline2.py` (resumable; per-round durable checkpoints in each build's
  `checkpoints/`, milestones with strategies). Phase D checks: `src/validate2.py` (conservation,
  convergence log, river error).
- Phase 0, 100bb: RFI 9.3/11.5/14.7/16.2/25.1/37.7/46.1 -> 9.7/12.6/15.3/16.9/24.7/39.8/52.4
  (R refit on all 100 flops, 50/50 with the final R); SB limp 4.1 -> 0.7.
- Phase 0 done (11:43). RFI UTG/UTG1/LJ/HJ/CO/BTN/SB, final -> refit on all 100 flops:
  200bb 11.4/12.7/14.8/19.3/24.5/35.3/51.5 -> 11.2/12.9/16.7/20.1/25.3/34.1/53.7;
  300bb 10.0/11.8/14.8/18.4/20.8/38.2/38.7 -> 10.6/13.4/16.7/18.4/22.2/38.4/44.0 (SB limp 15.8 -> 0.1).
  Moves are of the size of the old loops' noise. Kept as interim results (work/p0 in each build),
  not swapped into the delivered files: the stored postflop strategies belong to the old ranges.
- Phase B measurements running.

## Phase B gate decisions (2026-11-06; raw numbers in work/phaseB.txt)
- **Two raises per street: everywhere.** Turn-end solve cost 1.6-1.9x, memory ~2x (gate <= 3x).
- **River (item 8): reduced river tree** (user decision): the full river tree does not fit at 200bb/300bb
  in single-raised pots (12-35 GB). Reduced tree = one river bet size 66% + all-in, facing a river bet
  fold or call only; flop and turn unchanged (2 raises). 40 iterations, BTN vs BB AcJc8d, 1 thread:
  100bb SRP 371 s / 6.8 GB, 3bp 53 s / 1.1 GB; 200bb SRP 435 s / 8.5 GB, 3bp 179 s / 3.5 GB;
  300bb SRP 755 s / 12.0 GB, 3bp 221 s / 4.5 GB. Run one at a time on 4 threads, cap 400 iterations.
  Sample: srp_BTN_vs_BB, srp_SB_vs_BB, 3bp_BTN_vs_BB, 3bp_CO_vs_BTN x 4 flops (0, 25, 50, 75) per depth.
  The rake variants use the main loop's river correction (a ratio river/turn-end), not their own.
  Stored strategies stay turn-end solves.
- **Preflop tree: un-pruned (cold 4-bets and multiway 3-bet pots).** 100 iterations on 4 threads:
  100bb 8 s pruned / 46 s cold4 / 95 s cold4+mw3bp; 300bb 14 / 61 / 124 s. Final solve 5,000
  iterations ~1.5-1.7 h; loop rounds 600 warm-started iterations ~10-12 min.
- **Flops:** all 100 in every final pass. Plans: plan_100.json, plan_200.json, plan_300.json.
- 22:00 container restart; pipeline resumed (river SB vs BB Kc9c7d from its 21:27 checkpoint, ~30 min lost)
- 06:24 (Nov 7) container restart during main round 1 (calibration solves 173/390 of the 50-flop group done); pipeline resumed, solves in flight lost (~5 min)
- Budget (Nov 7): a main-loop round at 100bb takes ~6.5 h (30/50 flops per spot, two raises, ~490 solves), so 100bb Phase C is ~100+ h instead of ~18 h. Asked the user; decision: keep the plan as is (30/50 flops, full rake-variant loops).
- 200bb river step (Nov 9): with two postflop raises, 7 of 8 single-raised river solves need 12.5-21 GB
  (limit 12.5 GB) and were skipped. Decision: single-raised pots are measured with ONE postflop raise
  on both sides of the ratio (turn-end and river), plan key `river_srp_max_raises`; 3-bet pots keep two.
  Needs at 200bb: 7.1-7.6 GB (7c5d2h: BTN 11.8 GB, SB 12.55 GB -> skipped). Same setting for 300bb.
- 200bb river correction done (Nov 9 22:53): measured srp_BTN_vs_BB (BTN x1.17, BB x0.83) and 3bp_CO_vs_BTN (BTN x1.16, CO x0.87). srp_SB_vs_BB and 3bp_BTN_vs_BB had almost no calling range in the start solution (0.58 / 0.72 combos) and were skipped by the fit; those lines use the nearest group.
- Nov 10 01:07 container restart during 200bb main round 1 (91/100 slow-spot solves done); pipeline resumed, solves in flight lost (~5 min)
- Nov 11 01:59 container restart during 200bb main round 5 (58/100 slow-spot solves done); pipeline resumed, solves in flight lost (~5 min)
