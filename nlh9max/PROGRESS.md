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
- Postflop speed-ups (15:50): turn-equity tables stored as exact uint8 half-win counts out of 88
  (1 byte instead of 4: 417 MB -> 83 MB for BTN v BB, and cache-resident), hand classes under 1%
  reach dropped (<= 0.3% of range mass, see spots.json "trimmed_mass"), vectorised equity
  products. BTN v BB SRP 100bb: 64-141 s per flop on one core (was 393-748 s), 80-120 MB.
  Verified against the exact river sweep (same spot, 100 iterations): EV within 0.013 bb,
  strategy within 7.8 points on one action, values 18.154 vs 18.154 / 8.227 vs 8.227.
- Spot count: 14 of the 15 spots in the task are reachable; `srp_CO_vs_BTN` (CO opens, BTN flats)
  has an empty BTN range - in this high-rake equilibrium the BTN 3-bets or folds against a CO
  open and never cold-calls. Noted in REPORT.md as a structural result, not a reduction.
- Budget (measured): one full pass = 14 spots x 100 flops. SRP spots average ~90 s per flop,
  3-bet ~25 s, 4-bet ~10 s on one core => ~11 core-hours per pass, ~2.8 h on 4 cores. Three
  passes (stage 4 calibration, stage 7 final, plus the rake-off control and sensitivity runs of
  section 5) fit the budget with the full 100-flop set, so no flop cut is needed.
- Stage 4 done (16:26): 168 calibration solves (14 spots x 12 flops), all <= 0.5% of pot.
  `srp_CO_vs_BTN` empty (BTN never flats a CO open).
- Stage 5: R fit. First form (pairwise-normalised R) was degenerate (only R ratios identified,
  values ran to the 0.05 / 5.0 clips); replaced by the task's literal model share = R x equity x
  (pot - rake), with the two players' shares normalised to the net pot per terminal. Measured:
  SRP rake ~19% of the flop pot, 3-bet pots 12-15%, 4-bet 6-9% (cap); IP opener R 1.13-1.34,
  BB caller 0.73-0.80, OOP 3-bettor 1.03-1.17.
- Preflop <-> R rounds (damped 50/50 from n2): RFI UTG/UTG1/LJ/HJ/CO/BTN/SB
  n1 9.7/12.1/14.9/17.7/25.2/33.8/44.5, n2 9.9/11.9/15.5/16.6/25.6/40.8/45.0,
  n3 9.6/12.3/15.1/16.8/25.9/39.8/49.9. UTG-CO within 0.4 points; BTN (-1.0) and SB (+4.9)
  not yet; rounds n4, n5 running. Result so far: with 19% rake on flops and no rake on hands
  won preflop, the BB 3-bets or folds against a BTN open (3-bet ~21%, call ~0-1%).
