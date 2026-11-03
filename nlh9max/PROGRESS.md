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
