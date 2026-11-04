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
- 01:36 container restart: pipeline stopped; nothing lost on disk (equity, stage 3, 9 calibration
  solves); pipeline restarted and resumed.
- Stage 4-5 done (02:00): 168 calibration solves, 1.9 core-hours (~25 min on 4 cores). Mean
  seconds per flop: SRP SB v BB 181, BTN v BB 136, CO v BB 73, HJ v BB 48, UTG v BB 29; 3-bet
  pots 5-24; 4-bet 1-4. **Budget: a full 100-flop pass is ~16 core-hours (~4 h), so all 100 flops
  are kept for every spot; no flop cut is needed.**
  R (stage-3 ranges): SRP IP opener 1.15-1.20, BB caller 0.71-0.77, rake 19% of the flop pot;
  3-bet pots rake 12-15%; 4-bet pots at the 5bb cap (6.7-9.1% of the pot).
- Preflop <-> R rounds n1-n5 running (pipeline).
- Rounds (RFI UTG/UTG1/LJ/HJ/CO/BTN/SB): n1 11.1/12.5/14.1/20.4/26.9/36.2/47.8,
  n2 11.0/11.5/13.7/17.1/25.7/37.4/46.2, n3 10.9/11.8/13.6/17.6/25.2/35.5/49.0,
  n4 11.7/12.7/17.0/20.9/23.8/35.2/52.9, n5 11.8/12.9/16.6/20.0/24.4/35.0/51.4.
  Criterion (<= 0.5 points) not met; n4 jumped 3-4 points for LJ/HJ/SB, n5 moved <= 1.5.
- Stage 6 done (05:02): final preflop solve with the mean R of n3-n5: 11.4/12.7/14.8/19.3/24.5/
  35.3/51.5 (SB limp 3.8).
- Stage 7 running: 12 reachable spots x 100 flops = 1,200 solves (344 done at 05:55).
