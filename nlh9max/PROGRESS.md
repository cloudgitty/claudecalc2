# NLH 8-handed high-rake GTO build: progress

Task: `docs/TASK.md` (the prompt as given). One-line status after each stage, newest last.

## Standing instructions from the user
- 2026-11-03: **do not cut flops** – keep the full ~100-flop set for every postflop spot;
  budget reductions may only come from other levers (river detail, spot count, time), and
  must be reported before they are made.

## Stages
- 2026-11-03 14:18 UTC start. Machine: 4 cores, 15 GB RAM, gcc 13 with OpenMP, Python 3.13 +
  numpy 2.5 (numba not installed; the hot code is C).
- Stage 1 (tree and equity tables): in progress. Exact HU preflop equity for all 1326 x 1326
  combo pairs (47,008 suit/seat-symmetry classes x 1,712,304 boards) running (~30 min).
  Preflop tree (config.json rules, multiway pots only single-raised): 6,775 nodes.
- Stage 1 done (14:47): exact equity table (25 min, 47,008 classes; AA v KK 0.8195, AKs v QQ
  0.4605, 72o v AA 0.1180). 100 flops clustered (`flops.json`), `combos.json` written.
- Stage 2 done: rake rule unit tests pass (`make test`: C solver showdown/fold/tie/cap, Python
  rule). Postflop solver: rake, 33% donk lead, flop+turn with an exact river-averaged equity
  showdown (precomputed per turn card, vectorised: ~1 s/iteration for BTN v BB at 100bb).
- Postflop throughput (BTN v BB SRP, 100bb, stand-in ranges): 170 iterations to 0.5% of pot,
  174 s on one core, 200 MB. Projection for all 15 spots x 100 flops: ~50 core-hours per pass
  (~13 h on 4 cores); SRP spots dominate.
- Stage 3 done: first preflop solve with fixed R (IP 1.0 / OOP 0.85, 3-way 0.85/0.92/1.0):
  3,000 DCFR iterations, 10 min, worst exploitability 0.013 bb/100. Fixes on the way: 3-way
  payoff model refitted on 20,000 exact 3-way all-ins (additive model); call rule and cold
  4-bet pruning (tree 8,057 nodes). RFI UTG 9.8 UTG1 11.6 LJ 14.3 HJ 17.7 CO 24.5 BTN 35.9
  SB 30.2 (+24.4 limp).
- Stage 4 started: calibration postflop solves, 15 spots x 12 flops, ranges from stage 3.
