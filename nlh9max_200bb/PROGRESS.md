# NLH 8-handed high-rake build at 200bb: progress

Task: `docs/TASK.md`. Code base: the 100bb build in `../nlh9max` (same engine, same rules), with
the 200bb changes below. **No 100bb result is reused**: equities, flops, trees, R fits and all
solves are recomputed here.

## Standing instructions from the user
- From the 100bb build (2026-11-03): do not cut flops. The 200bb task allows cutting to 30 flops
  first; the full 100-flop set is kept unless the measured budget makes that impossible, and any
  cut is reported before it is made.

## Stages
- 2026-11-04 00:46 UTC start. 4 cores, 15 GB, gcc 13 + OpenMP, Python 3.13 + numpy.
- 200bb changes: `config.json` stack 200; postflop rule "a listed size putting in more than 60% of
  the remaining stack on a non-river street is also an all-in option" (`allin_extra_frac` in the
  solver, flop and turn); spot stacks read from the config.
- Stage 1 (tree and equity): preflop tree 16,395 nodes (8,412 decisions, 1,656 flop terminals,
  3,585 all-in). 4-bet cap rule (all-in if the 4-bet reaches 40% of the stack): **never triggers**
  (617 4-bet decisions, largest 4-bet 50.4bb < 80bb). Exact equity table recomputing (~25 min).
  100 flops (`flops.json`), `combos.json`.
- Stage 2: rake unit tests pass with the 200bb solver build.
- Postflop throughput (stand-in ranges, BTN v BB SRP, 197bb behind): 33,126 nodes, 166 MB,
  ~1 s/iteration on one core, 6.3% of pot after 60 iterations (100bb: ~3%) - deeper trees need
  more iterations.
- Stage 1 done: exact equity table 22 min (85 core-min).
- Stage 3 done (01:29): fixed-R preflop solve, 3,000 iterations, worst 0.051 bb/100. RFI UTG 9.9
  UTG1 11.7 LJ 14.3 HJ 18.0 CO 23.8 BTN 34.3 SB 32.3 (+23.0 limp). Spot SPRs: SRP 26-30, 3-bet
  pots IP 9.0-9.8 / OOP 6.1, 4-bet pots 2.2-3.4 (as the task expects).
- Stage 4 running: calibration solves 14 spots x 12 flops on the stage-3 ranges.
