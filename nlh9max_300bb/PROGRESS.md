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
