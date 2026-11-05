# NLH 8-handed high-rake build at 300bb: progress

Task: `docs/TASK.md`. Code base: the 200bb build (`../nlh9max_200bb`). Reused, as the task allows:
code, the exact hand equity tables (`work/eq*.bin`) and the flop clustering (`flops.json`).
Everything else (trees, R, all solves) is computed at 300bb.

## Standing instructions from the user
- Do not cut flops (given on the 100bb build). The 300bb task allows cutting to 24; all 100 are
  kept unless the measured budget makes that impossible, and any cut is reported first.

## Stages
- 2026-11-04 19:12 UTC start.
- 300bb changes: stack 300; postflop "all-in as well as the listed size when it puts in more than
  50% of the remaining stack on a non-river street" (`allin_extra_frac` 0.50, flop and turn);
  per-class R for solved spots (speculative hands keep their own measured realisation, shrunk
  toward the strength-bucket value with a 6-combo prior); postflop divergence guard (a run that
  ends above target and above 1.5x its best point is re-solved to the best iteration count;
  non-finite results are written as 999999 so the JSON stays valid).
- Stage 1: tree 16,395 nodes (8,412 decisions, 1,656 flop / 3,585 all-in / 2,742 fold terminals).
  The 4-bet cap (40% of the stack = 120bb) **never triggers**: 617 4-bet decisions, largest 50.4bb.
  After a 4-bet the 3-bettor may fold / call / jam 300bb, and facing the jam the 4-bettor folds or
  calls (pot odds from the actual investments).
- Stage 2: rake unit tests pass.
- Postflop throughput (stand-in ranges, BTN v BB SRP, 297bb behind): 33,126 nodes, 166 MB, ~0.8 s
  per iteration, 9.6% of pot after 60 iterations (200bb: 6.3%).
- 19:22-22:02 container down (no watcher running, session looked idle); pipeline resumed from the
  saved preflop state at iteration 1,000.
- Stage 3 done (22:14): fixed-R solve, RFI 10.6/12.1/14.5/17.9/24.0/34.7/33.1 (SB limp 21.5).
- Stage 4-5 done (22:57): 180 calibration solves (all 15 spots reachable), all <= 0.5% of pot,
  no divergence. 2.9 core-hours; SRP solves need up to 650 iterations (SB v BB 266 s, BTN v BB
  160 s per flop). **Budget: full 100-flop pass ~24 core-hours (~6 h); all 100 flops kept.**
- Rounds: n1 14.0/15.5/17.5/23.1/27.3/40.8/55.2, n2 10.9/14.0/17.0/17.8/20.9/36.8/45.1.
