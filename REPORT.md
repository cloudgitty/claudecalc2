# PLO6 6-max 200bb preflop ranges: report

**Read this first.** These ranges are a self-consistent set of best responses inside a simplified
model: heads-up decisions only, postflop replaced by realised equity `R * eq * pot`. They are **not a
solver equilibrium**. The opening ranges come out much tighter than real PLO6 play (LJ about 5.5%,
BTN about 20%), and the defending ranges much wider (BB continues 69 to 76%). The reasons are under
"Known weak points". Treat the output as a model result to review, not as ground truth.

## Parameters

- PLO6, 6-max, 200bb effective, pot-limit, exactly two hole cards + three board cards. No rake, no ante.
  Blinds 0.5/1.
- Sizes: open 3bb (SB to 4). 3-bet to 10.5 in position, 12 from the blinds. Pot-sized 4-bet
  (`F = 3T + dead`: 33 vs 10.5, 36.5 vs a BB 3-bet with the SB dead, 36 SB vs BB, 37 BTN vs an SB
  3-bet). The 5-bet is all-in (200).
- Every heads-up confrontation (opener O, responder D) is solved as a five-node tree: O1 open or fold;
  D1 fold, call or 3-bet; O2 fold, call or 4-bet; D2 fold, call or jam; O3 fold or call the jam.
  All 15 (O, D) pairs are solved, including responders with no output spot (HJ/CO/BTN/SB vs earlier
  opens), because the opener's RFI EV needs every seat behind it. 24 nodes are output.
- RFI EV = P(all fold) * 1.5 + sum over responders in seat order of P(earlier ones fold) *
  [P(call) * EV(called) + P(3-bet) * EV(best response to the 3-bet)].
- Realisation: `R = R0 * (1 + 0.15 z)`. z is the playability z-score (nut-made wins minus
  straight-or-better losses vs that villain range), clipped to +-2 and standardised over the
  combos reaching the decision. All-in pots use R = 1.
- R0 used in the solve: 1.00 IP / 0.82 OOP in single-raised and 3-bet pots, 0.90 IP / 0.82 OOP
  in 4-bet pots. Cold calls get a players-behind factor 0.878^k (see below), which gives the SB its 0.72.
- Mixed frequencies: linear ramp over +-1% of reaching combos around each indifference point,
  rounded to 5%. Damping 0.5. Round cap 12, plus a warm-up and refinement rounds (see below).

## Assumptions changed or added (every deviation from the prompt)

1. **R0: calibrated, then not used. This is the biggest deviation.** Stage 4 measured R0 with
   40,000 flop-to-river simulations per configuration under a fixed, identical-for-both-seats
   equity-bucket strategy. Measured: SRP OOP 0.810 / IP 1.177, 3BP 0.856 / 1.157, 4BP 0.826 / 1.175
   (`work/r0.json`). A first strategy (absolute equity thresholds vs the unconditioned range) gave
   R_oop 0.0 to 0.4 at SPR 30. That is an artefact of value-only betting and was discarded. The
   percentile strategy that replaced it still gives 1.26 / 0.73 in a control run with identical
   ranges on both seats. So the measurement mostly reflects how exploitable the fixed strategy is,
   not real positional value. Solving with the measured values unravelled the game: with IP R0 about
   1.17, every IP call or 3-bet with about 40% equity is profitable. PLO6 equities cluster around
   40 to 60%, so BTN 3-bet 33% of all hands vs CO and the opens collapsed to LJ 1.5 / HJ 2.2 /
   CO 4.7 / SB 1.9 while still falling. The final solve therefore uses the prompt's default R0. The
   measured values stay in the report. The prompt's "measured, not guessed" requirement is **not met**.
2. **Players-behind factor for cold calls.** The prompt's SB default (0.72 vs 0.82 OOP, "BB behind")
   is a penalty of 0.72/0.82 = 0.878 for one player left to act. It is applied to every flat call of
   an open, once per player left behind the caller: HJ 4, CO 3, BTN 2, SB 1, BB 0. So BTN flats vs
   CO with R0 0.77 instead of 1.00. Without it, IP cold-callers in the heads-up model call 60 to
   87% and early opens collapse (LJ 4.4% and still falling in a test run).
3. **Census instead of a stratified sample.** The engine runs 12M trials/s, so all 962,988 classes
   were simulated at 4,000 boards against each of 12 reference bands, not a stratified 80,000.
   The feature model and its holdout validation are still built as specified. EB shrinkage applies
   to every class.
4. **Reference villain ranges = 12 strength bands, combined as mixtures.** The bands are
   combo-percentile slices 0-1.5, 1.5-3, 3-5, 5-8, 8-12, 12-17, 17-23, 23-30, 30-40, 40-55, 55-75 and
   75-100%. Strength order = equity vs the top 25% (pilot simulation). The equity against any range
   during the iteration is the band-weight mixture of the per-band equities. Each band equity
   includes exact card removal; the mixture weights do not adapt to the hero's cards. The borderline
   re-simulation then replaces this approximation with exact equities for every class near a cutoff.
5. **Warm-up before round 1.** Starting all nodes from crude initial responses made the
   simultaneous best-response update oscillate (BB 3-bet 60%+, LJ open 60%). The response nodes are
   first iterated against the fixed starting opens (14 sweeps), then the 12 counted rounds run.
6. **Refinement rounds after the re-simulation.** Six more damped rounds with the exact equities
   in place. These come on top of the 12-round cap.
7. **Fold equity with card removal.** Villain action probabilities given the hero's hand are
   computed by sampling villain hands that do not collide with the hero (48 per class), averaged
   within the hero's rank multiset to remove noise. Suit-level blocking of fold frequencies is
   therefore ignored (it is kept in the equities).
8. **Borderline band narrowed.** The plan was +-2% of reaching combos. Narrowed to +-1.25% (RFI too)
   to stay inside the 60-minute re-simulation budget. A few thousand (class, range) tasks whose
   range almost always collides with the hero's cards (fewer than 6,000 valid trials out of 12,000)
   are dropped and keep the mixture value.
9. Unreachable classes get byte 0 (prompt rule). O2 spots are masked by "open > 0" and D2 spots by
   "3-bet > 0". The frequency stored is the decision frequency conditional on reaching the node.

## Method

1. Stage 1: enumerate all C(52,6) hands in C, canonical code per the reference algorithm (verified
   against the Python reference on 3,000 random hands), `np.unique`. Combos are checked against
   24/|stabiliser| on 3,000 classes.
2. Stage 2: Omaha evaluator (15 hole pairs x 10 board triples via a 91 x 455 rank table plus a
   flush table), checked against an independent brute-force evaluator. Villain pools are 400,000
   concrete hands sampled by weight, with rejection on collisions. Each trial picks a uniformly random
   suit representative of the hero class, so class equity is the mean over its representatives.
3. Stage 3: LightGBM on 65 features (rank counts, suit pattern, per-suit top ranks, nut / K / Q
   flush draws, pairs and trips, straight windows and straight-capable pairs, runs, span, danglers,
   high-card sums, sorted ranks). 5-fold cross-fit, so each fold is a 20% holdout. Empirical-Bayes
   posterior per class: `(y/se^2 + m/tau^2)/(1/se^2 + 1/tau^2)`, with tau^2 = holdout MSE minus
   mean simulation variance.
4. Stage 4: R0 calibration (see the assumptions above).
5. Stage 5: iteration. Stage 6: borderline re-simulation vs the actual ranges (exact removal,
   adaptive trials until SE <= 0.3 pts), corrections = exact minus mixture, refinement rounds.
6. Stage 7: quantise to 5%, pack, widths, test vectors. Stage 8: checks and this report.


## Summary per spot

Widths (as in widths.json) are combo-weighted shares of all 20,358,520 hands, using each class's
stored frequency (which is conditional on reaching the node). "Reach" is the share of all hands
that get to the decision: all hands for RFI and defend spots, the opening range (weighted by open
frequency) for opener vs 3-bet, and the 3-bet range for 3-bettor vs 4-bet. The two "% of reach"
columns are reach-weighted averages of the stored frequencies.

| spot | classes played | raise % of all hands | continue % of all hands | reach % of all hands | raise % of reach | continue % of reach |
|---|---|---|---|---|---|---|
| LJ_RFI | 59,442 | 5.52 | 5.52 | 100.00 | 5.5 | 5.5 |
| HJ_RFI | 64,656 | 6.09 | 6.09 | 100.00 | 6.1 | 6.1 |
| CO_RFI | 83,481 | 8.15 | 8.15 | 100.00 | 8.1 | 8.1 |
| BTN_RFI | 190,282 | 19.74 | 19.74 | 100.00 | 19.7 | 19.7 |
| SB_RFI | 79,015 | 7.72 | 7.72 | 100.00 | 7.7 | 7.7 |
| BB_vs_LJ | 659,025 | 1.93 | 69.44 | 100.00 | 1.9 | 69.4 |
| BB_vs_HJ | 666,077 | 2.06 | 70.16 | 100.00 | 2.1 | 70.2 |
| BB_vs_CO | 681,442 | 2.39 | 71.75 | 100.00 | 2.4 | 71.7 |
| BB_vs_BTN | 721,749 | 4.22 | 75.88 | 100.00 | 4.2 | 75.9 |
| BB_vs_SB | 688,959 | 6.47 | 72.48 | 100.00 | 6.5 | 72.5 |
| SB_vs_CO | 222,681 | 2.76 | 23.56 | 100.00 | 2.8 | 23.6 |
| SB_vs_BTN | 292,611 | 5.28 | 31.03 | 100.00 | 5.3 | 31.0 |
| BTN_vs_CO | 170,908 | 11.32 | 17.69 | 100.00 | 11.3 | 17.7 |
| LJ_vs_BB3bet | 51,097 | 2.24 | 5.48 | 5.52 | 38.1 | 87.6 |
| HJ_vs_BB3bet | 55,147 | 2.22 | 5.95 | 6.09 | 34.7 | 87.0 |
| CO_vs_BB3bet | 70,176 | 2.28 | 7.63 | 8.15 | 27.3 | 85.7 |
| BTN_vs_BB3bet | 160,552 | 2.84 | 17.46 | 19.74 | 14.3 | 85.5 |
| SB_vs_BB3bet | 62,888 | 2.05 | 6.89 | 7.72 | 25.4 | 81.0 |
| CO_vs_BTN3bet | 72,158 | 1.79 | 7.87 | 8.15 | 21.9 | 90.3 |
| BTN_vs_SB3bet | 168,407 | 2.96 | 18.31 | 19.74 | 14.9 | 89.5 |
| BB_vs_BTN4bet | 43,638 | 3.55 | 4.63 | 4.22 | 81.0 | 94.6 |
| BB_vs_SB4bet | 63,901 | 3.30 | 6.80 | 6.47 | 50.8 | 93.6 |
| SB_vs_BTN4bet | 51,775 | 3.87 | 5.52 | 5.28 | 71.2 | 93.3 |
| BTN_vs_CO4bet | 101,982 | 3.06 | 11.03 | 11.32 | 27.0 | 91.6 |

## Convergence log

Main iteration (damping 0.5, tolerance: width change <= 0.3 pts and mean byte-frequency
change <= 2 pts, two rounds in a row; the frequency change is the combo-weighted mean of
|d call| + |d raise| over classes in the spot's range, averaged over the 24 spots):

| round | max width change (pts) | mean byte-freq change (pts) | worst spot (pts) | LJ / HJ / CO / BTN / SB open % |
|---|---|---|---|---|
| main 1 | 11.67 | 6.28 | 29.05 | 14.4 / 18.2 / 24.3 / 41.9 / 30.3 |
| main 2 | 7.53 | 7.61 | 22.47 | 10.8 / 13.7 / 19.1 / 36.9 / 22.8 |
| main 3 | 5.11 | 7.93 | 24.19 | 8.3 / 10.6 / 15.3 / 32.7 / 17.7 |
| main 4 | 4.10 | 7.67 | 20.62 | 6.9 / 8.9 / 13.1 / 29.7 / 14.9 |
| main 5 | 8.59 | 11.08 | 32.18 | 5.9 / 7.6 / 11.3 / 27.1 / 12.6 |
| main 6 | 5.15 | 10.14 | 23.19 | 5.3 / 6.7 / 10.1 / 25.2 / 11.1 |
| main 7 | 3.81 | 8.00 | 23.93 | 5.1 / 6.4 / 9.4 / 23.9 / 10.2 |
| main 8 | 2.23 | 6.10 | 18.99 | 5.3 / 6.4 / 9.1 / 22.9 / 9.5 |
| main 9 | 1.48 | 4.13 | 11.84 | 5.7 / 6.5 / 8.9 / 22.1 / 9.0 |
| main 10 | 1.52 | 3.61 | 10.53 | 5.9 / 6.6 / 8.7 / 21.5 / 8.6 |
| main 11 | 0.95 | 2.44 | 6.42 | 6.0 / 6.6 / 8.5 / 21.0 / 8.3 |
| main 12 | 0.62 | 1.87 | 4.44 | 5.9 / 6.5 / 8.4 / 20.7 / 8.0 |

Warm-up (opens held at the starting widths, response nodes only):

| round | max width change (pts) | mean byte-freq change (pts) | worst spot (pts) | LJ / HJ / CO / BTN / SB open % |
|---|---|---|---|---|
| warm-up 1 | 37.52 | 36.19 | 62.49 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 2 | 26.92 | 41.90 | 73.00 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 3 | 36.10 | 34.72 | 57.50 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 4 | 26.17 | 20.70 | 34.47 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 5 | 19.26 | 19.06 | 40.89 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 6 | 16.29 | 20.44 | 36.73 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 7 | 12.61 | 21.20 | 44.59 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 8 | 11.78 | 20.12 | 40.85 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 9 | 23.79 | 18.29 | 52.92 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 10 | 10.81 | 15.41 | 49.23 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 11 | 5.83 | 11.66 | 40.37 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 12 | 5.53 | 8.90 | 35.47 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 13 | 4.93 | 6.53 | 23.60 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 14 | 3.57 | 4.93 | 14.98 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 15 | 2.24 | 3.73 | 13.26 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 16 | 1.65 | 2.57 | 7.63 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 17 | 0.85 | 1.89 | 6.64 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 18 | 2.12 | 1.63 | 10.21 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 19 | 1.86 | 1.35 | 8.52 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |
| warm-up 20 | 1.20 | 1.13 | 4.67 | 20.0 / 25.0 / 32.0 / 48.0 / 42.0 |

Refinement rounds after the borderline re-simulation (exact equities in place):

| round | max width change (pts) | mean byte-freq change (pts) | worst spot (pts) | LJ / HJ / CO / BTN / SB open % |
|---|---|---|---|---|
| refine 1 | 0.72 | 4.39 | 10.11 | 5.7 / 6.3 / 8.2 / 20.2 / 7.8 |
| refine 2 | 0.51 | 3.60 | 6.08 | 5.6 / 6.2 / 8.1 / 19.9 / 7.7 |
| refine 3 | 0.24 | 2.80 | 6.48 | 5.5 / 6.1 / 8.1 / 19.8 / 7.7 |
| refine 4 | 0.31 | 2.61 | 6.52 | 5.5 / 6.1 / 8.1 / 19.8 / 7.7 |
| refine 5 | 0.84 | 3.18 | 12.42 | 5.5 / 6.1 / 8.1 / 19.7 / 7.7 |
| refine 6 | 0.06 | 1.09 | 3.07 | 5.5 / 6.1 / 8.1 / 19.7 / 7.7 |

## Validation

**Counts and encoding.** n = 962,988 classes, combos sum = 20,358,520,
largest byte = 230 (every byte decodes to call + raise <= 100: True).

**Round trip.** An independent decoder (`src/validate_decode.py`, written from the spec, with no
encoder code) inflates the planes, checks the 962,988 codes are strictly ascending, and compares
100,000 random (class, spot) pairs with the solver state: 0 mismatches.
All 300 test vectors replay (class code by the reference algorithm, class id by
binary search, all 24 bytes): 0 failures.

**Monotonic sanity.** {"rfi_monotone LJ<HJ<CO<BTN": true, "bb_defend_monotone LJ<HJ<CO<BTN": true, "vs_BB3bet_continue_monotone": true, "4bet_narrower_than_3bet (4bet width, 3bet width, ok)": {"BB_vs_BTN4bet": [2.82, 4.22, true], "BB_vs_SB4bet": [1.96, 6.47, true], "SB_vs_BTN4bet": [2.95, 5.28, true], "BTN_vs_CO4bet": [1.79, 11.32, true]}, "4bet raises that are pure RFI folds": {"LJ_vs_BB3bet": 0, "HJ_vs_BB3bet": 0, "CO_vs_BB3bet": 0, "BTN_vs_BB3bet": 0, "SB_vs_BB3bet": 0, "CO_vs_BTN3bet": 0, "BTN_vs_SB3bet": 0}}

**Feature model** (LightGBM on 65 hand-structure features, 5-fold cross-fit so that every
class's model value comes from a model that did not see it; each fold is a 20% holdout).
The raw holdout targets are 4,000-board simulations whose own noise (about 0.75 pts SE) caps
the R2 any model can reach against them, so the table also scores the out-of-fold model on a
precision holdout: 4,000 random classes re-simulated at 64,000 boards per band (target SE about 0.19 pts).

| band | range (% of hands, strength order) | holdout R2 raw (4k-board targets) | noise-corrected R2 | Spearman raw | precision-holdout R2 model | precision-holdout Spearman model | RMSE raw sim / model / posterior (pts) |
|---|---|---|---|---|---|---|---|
| 0 | 0-1.5 | 0.9635 | 0.9978 | 0.9669 | 0.9957 | 0.9958 | 0.79 / 0.27 / 0.27 |
| 1 | 1.5-3 | 0.9674 | 0.9979 | 0.9709 | 0.9960 | 0.9962 | 0.78 / 0.28 / 0.27 |
| 2 | 3-5 | 0.9699 | 0.9979 | 0.9721 | 0.9964 | 0.9966 | 0.80 / 0.28 / 0.27 |
| 3 | 5-8 | 0.9717 | 0.9980 | 0.9752 | 0.9965 | 0.9968 | 0.79 / 0.28 / 0.28 |
| 4 | 8-12 | 0.9735 | 0.9982 | 0.9794 | 0.9969 | 0.9976 | 0.79 / 0.28 / 0.27 |
| 5 | 12-17 | 0.9751 | 0.9984 | 0.9830 | 0.9972 | 0.9981 | 0.79 / 0.27 / 0.27 |
| 6 | 17-23 | 0.9761 | 0.9984 | 0.9854 | 0.9972 | 0.9983 | 0.79 / 0.28 / 0.27 |
| 7 | 23-30 | 0.9769 | 0.9984 | 0.9868 | 0.9973 | 0.9985 | 0.80 / 0.28 / 0.27 |
| 8 | 30-40 | 0.9776 | 0.9986 | 0.9877 | 0.9973 | 0.9985 | 0.80 / 0.28 / 0.27 |
| 9 | 40-55 | 0.9781 | 0.9986 | 0.9880 | 0.9974 | 0.9986 | 0.78 / 0.28 / 0.27 |
| 10 | 55-75 | 0.9778 | 0.9986 | 0.9872 | 0.9974 | 0.9985 | 0.80 / 0.28 / 0.27 |
| 11 | 75-100 | 0.9765 | 0.9985 | 0.9846 | 0.9973 | 0.9982 | 0.80 / 0.28 / 0.27 |

Playability model (nut-minus-reverse-nut potential), noise-corrected holdout R2 per band:
0.998, 0.997, 0.997, 0.997, 0.997, 0.998, 0.998, 0.998, 0.998, 0.998, 0.998, 0.998.

**Borderline re-simulation.** Band = classes within +-1.25% of reaching combos of a
cutoff (RFI +-1.25%), in every output spot. Classes per spot: {"LJ_RFI": 22537, "HJ_RFI": 22543, "CO_RFI": 22569, "BTN_RFI": 23269, "SB_RFI": 22515, "BB_vs_LJ": 48217, "BB_vs_HJ": 48240, "BB_vs_CO": 48182, "BB_vs_BTN": 47022, "BB_vs_SB": 47671, "SB_vs_CO": 46328, "SB_vs_BTN": 45819, "BTN_vs_CO": 43833, "LJ_vs_BB3bet": 3680, "HJ_vs_BB3bet": 3943, "CO_vs_BB3bet": 4720, "BTN_vs_BB3bet": 10594, "SB_vs_BB3bet": 5031, "CO_vs_BTN3bet": 4676, "BTN_vs_SB3bet": 10916, "BB_vs_BTN4bet": 3316, "BB_vs_SB4bet": 3882, "SB_vs_BTN4bet": 3795, "BTN_vs_CO4bet": 6166}.
1,498,906 (class, actual range) pairs, 3.934e+10 trials; max SE 0.401 pts
(mean 0.295), share with SE <= 0.3 pts 99.97%;
1,068 degenerate tasks dropped (the range almost always collides with the hero's cards). Band-mixture approximation vs the exact equity against the actual range:
bias -0.76 pts, RMSE 1.44 pts, 95th percentile |error|
2.48 pts.

**First-principles checks** (no published numbers are quoted):
- Mean equity of a random hand vs a random hand must be exactly 50%: raw simulations
  49.985%, shrunk table 49.985%.
- Band a vs band b plus band b vs band a must be about 1: max deviation 0.0161.
- Exact enumeration of all C(40,5) = 658,008 boards vs the Monte Carlo sampler:

| hero | villain | exact eq % | MC eq % (32M boards) | z |
|---|---|---|---|---|
| As Ah Ks Kh Qd Jc | Ts 9h 8s 7h 6d 5c | 54.046 | 54.043 | -0.32 |
| As Ks Qh Jh Td 9c | Ac Ad 7s 6h 3d 2c | 51.624 | 51.621 | -0.28 |
| 2s 2h 2d 7s 8h 9c | Kc Qc Jd Td 9s 8d | 36.349 | 36.353 | +0.51 |

## Hand-level sanity (call % / raise %, spots where the hand plays)

- **AAKK ds + QJ** (As Ah Ks Kh Qd Jc, class 958626): LJ_RFI 0/100; HJ_RFI 0/100; CO_RFI 0/100; BTN_RFI 0/100; SB_RFI 0/100; BB_vs_LJ 5/95; BB_vs_HJ 0/100; BB_vs_CO 0/100; BB_vs_BTN 0/100; BB_vs_SB 0/100; SB_vs_CO 0/100; SB_vs_BTN 0/100; BTN_vs_CO 0/100; LJ_vs_BB3bet 0/100; HJ_vs_BB3bet 0/100; CO_vs_BB3bet 0/100; BTN_vs_BB3bet 0/100; SB_vs_BB3bet 0/100; CO_vs_BTN3bet 0/100; BTN_vs_SB3bet 0/100; BB_vs_BTN4bet 0/100; BB_vs_SB4bet 100/0; SB_vs_BTN4bet 0/100; BTN_vs_CO4bet 100/0
- **AAKK ds + 72** (As Ah Ks Kh 7d 2c, class 957334): LJ_RFI 0/100; HJ_RFI 0/100; CO_RFI 0/100; BTN_RFI 0/100; SB_RFI 0/100; BB_vs_LJ 45/55; BB_vs_HJ 45/55; BB_vs_CO 40/60; BB_vs_BTN 0/100; BB_vs_SB 0/100; SB_vs_CO 25/75; SB_vs_BTN 0/100; BTN_vs_CO 0/100; LJ_vs_BB3bet 100/0; HJ_vs_BB3bet 100/0; CO_vs_BB3bet 100/0; BTN_vs_BB3bet 95/5; SB_vs_BB3bet 45/55; CO_vs_BTN3bet 100/0; BTN_vs_SB3bet 30/70; BB_vs_BTN4bet 0/100; BB_vs_SB4bet 100/0; SB_vs_BTN4bet 0/100; BTN_vs_CO4bet 100/0
- **AAJT ds + 98** (As Ah Js Th 9d 8c, class 927820): LJ_RFI 0/100; HJ_RFI 0/100; CO_RFI 0/100; BTN_RFI 0/100; SB_RFI 0/100; BB_vs_LJ 0/100; BB_vs_HJ 0/100; BB_vs_CO 0/100; BB_vs_BTN 0/100; BB_vs_SB 0/100; SB_vs_CO 0/100; SB_vs_BTN 0/100; BTN_vs_CO 0/100; LJ_vs_BB3bet 0/100; HJ_vs_BB3bet 0/100; CO_vs_BB3bet 0/100; BTN_vs_BB3bet 0/100; SB_vs_BB3bet 0/100; CO_vs_BTN3bet 0/100; BTN_vs_SB3bet 0/100; BB_vs_BTN4bet 0/100; BB_vs_SB4bet 0/100; SB_vs_BTN4bet 0/100; BTN_vs_CO4bet 0/100
- **AAJT ds + 52** (As Ah Js Th 5d 2c, class 925980): LJ_RFI 0/100; HJ_RFI 0/100; CO_RFI 0/100; BTN_RFI 0/100; SB_RFI 0/100; BB_vs_LJ 0/100; BB_vs_HJ 0/100; BB_vs_CO 0/100; BB_vs_BTN 0/100; BB_vs_SB 0/100; SB_vs_CO 0/100; SB_vs_BTN 0/100; BTN_vs_CO 0/100; LJ_vs_BB3bet 0/100; HJ_vs_BB3bet 0/100; CO_vs_BB3bet 0/100; BTN_vs_BB3bet 0/100; SB_vs_BB3bet 0/100; CO_vs_BTN3bet 0/100; BTN_vs_SB3bet 0/100; BB_vs_BTN4bet 0/100; BB_vs_SB4bet 0/100; SB_vs_BTN4bet 0/100; BTN_vs_CO4bet 0/100
- **AKQJT9 ds** (As Ks Qh Jh Td 9c, class 882513): LJ_RFI 0/100; HJ_RFI 0/100; CO_RFI 0/100; BTN_RFI 0/100; SB_RFI 0/100; BB_vs_LJ 100/0; BB_vs_HJ 100/0; BB_vs_CO 100/0; BB_vs_BTN 0/100; BB_vs_SB 0/100; SB_vs_CO 100/0; SB_vs_BTN 0/100; BTN_vs_CO 0/100; LJ_vs_BB3bet 100/0; HJ_vs_BB3bet 100/0; CO_vs_BB3bet 100/0; BTN_vs_BB3bet 100/0; SB_vs_BB3bet 100/0; CO_vs_BTN3bet 100/0; BTN_vs_SB3bet 100/0; BB_vs_BTN4bet 100/0; BB_vs_SB4bet 100/0; SB_vs_BTN4bet 100/0; BTN_vs_CO4bet 100/0
- **KQJT98 ds** (Ks Qs Jh Th 9d 8c, class 524911): LJ_RFI 0/100; HJ_RFI 0/100; CO_RFI 0/100; BTN_RFI 0/100; SB_RFI 0/100; BB_vs_LJ 100/0; BB_vs_HJ 100/0; BB_vs_CO 100/0; BB_vs_BTN 100/0; BB_vs_SB 100/0; SB_vs_CO 100/0; SB_vs_BTN 100/0; BTN_vs_CO 0/100; LJ_vs_BB3bet 100/0; HJ_vs_BB3bet 100/0; CO_vs_BB3bet 100/0; BTN_vs_BB3bet 100/0; SB_vs_BB3bet 100/0; CO_vs_BTN3bet 100/0; BTN_vs_SB3bet 100/0; BTN_vs_CO4bet 100/0
- **A2345K ds** (As 2s 3h 4h 5d Kc, class 787336): BB_vs_LJ 100/0; BB_vs_HJ 100/0; BB_vs_CO 100/0; BB_vs_BTN 100/0; BB_vs_SB 100/0
- **A2345x ds (x=9)** (As 2s 3h 4h 5d 9c, class 602595): BB_vs_LJ 100/0; BB_vs_HJ 100/0; BB_vs_CO 100/0; BB_vs_BTN 100/0; BB_vs_SB 100/0
- **222 + 789 ds** (2s 2h 2d 7s 8h 9c, class 30784): fold everywhere
- **223344 ds** (2s 2h 3s 3h 4d 4c, class 38): fold everywhere
- **2233 + AK ds** (2s 2h 3d 3c As Kh, class 786552): BB_vs_LJ 100/0; BB_vs_HJ 100/0; BB_vs_CO 100/0; BB_vs_BTN 100/0; BB_vs_SB 100/0
- **444 + AKQ** (4s 4h 4d As Kh Qc, class 854992): fold everywhere
- **unconnected K9632 + 8? (2-2-1-1)** (Kc 8d 3h Js 5c 2h, class 417227): BB_vs_LJ 100/0; BB_vs_HJ 100/0; BB_vs_CO 100/0; BB_vs_BTN 100/0; BB_vs_SB 100/0
- **unconnected Q9742+K** (Qs 9h 7d 4c 2s Kh, class 480025): BB_vs_LJ 100/0; BB_vs_HJ 100/0; BB_vs_CO 100/0; BB_vs_BTN 100/0; BB_vs_SB 100/0
- **single suit AKQJT9** (As Ks Qs Js Ts 9s, class 882479): BB_vs_LJ 100/0; BB_vs_HJ 100/0; BB_vs_CO 100/0; BB_vs_BTN 100/0; BB_vs_SB 100/0
- **single suit Q T 8 6 4 2** (Qs Ts 8s 6s 4s 2s, class 232844): BB_vs_LJ 100/0; BB_vs_HJ 100/0; BB_vs_CO 100/0
- **AAA + KQJ** (As Ah Ad Kc Qs Jh, class 962766): LJ_RFI 0/35; HJ_RFI 0/35; CO_RFI 0/45; BTN_RFI 0/85; BB_vs_LJ 55/45; BB_vs_HJ 50/50; BB_vs_CO 50/50; BB_vs_BTN 50/50; BB_vs_SB 0/100; SB_vs_CO 45/55; SB_vs_BTN 10/90; BTN_vs_CO 0/100; LJ_vs_BB3bet 0/100; HJ_vs_BB3bet 5/95; CO_vs_BB3bet 100/0; BTN_vs_BB3bet 55/45; CO_vs_BTN3bet 5/0; BTN_vs_SB3bet 75/25; BB_vs_BTN4bet 0/100; BB_vs_SB4bet 0/100; SB_vs_BTN4bet 0/100; BTN_vs_CO4bet 0/100
- **AAA + 742** (As Ah Ad 7c 4s 2h, class 959046): BB_vs_LJ 80/20; BB_vs_HJ 95/5; BB_vs_CO 100/0; BB_vs_BTN 100/0; BB_vs_SB 0/100; SB_vs_CO 0/25; BTN_vs_CO 0/100; BB_vs_SB4bet 0/100; BTN_vs_CO4bet 70/30

## Sensitivity (change in raise / continue width, pts of all hands)

R0+0.05: converged=False in 10 rounds; R0-0.05: converged=False in 10 rounds; open 3.5bb: converged=False in 10 rounds; open 2.5bb: converged=False in 10 rounds. R0 +-0.05 shifts every R0 value; open +-0.5bb changes the open size (SB to 4 +- 0.5)
with 3-bet sizes and the pot-sized 4-bet unchanged in formula. Each rerun warm-starts from the final
state with the same equity corrections.

| spot | R0+0.05 | R0-0.05 | open 3.5bb | open 2.5bb |
|---|---|---|---|---|
| LJ_RFI | -0.12 / -0.12 | +2.34 / +2.34 | +1.44 / +1.44 | -0.62 / -0.62 |
| HJ_RFI | +0.16 / +0.16 | +2.93 / +2.93 | +1.65 / +1.65 | -0.77 / -0.77 |
| CO_RFI | +2.45 / +2.45 | +2.88 / +2.88 | +2.94 / +2.94 | -1.03 / -1.03 |
| BTN_RFI | +2.64 / +2.64 | +1.85 / +1.85 | +5.93 / +5.93 | -1.34 / -1.34 |
| SB_RFI | +0.71 / +0.71 | +0.76 / +0.76 | -0.11 / -0.11 | -0.15 / -0.15 |
| BB_vs_LJ | +0.14 / +8.22 | -0.00 / -8.00 | +0.01 / -10.87 | +0.01 / +15.74 |
| BB_vs_HJ | +0.18 / +8.10 | -0.01 / -8.26 | +0.01 / -10.77 | +0.03 / +15.27 |
| BB_vs_CO | +0.56 / +8.41 | -0.01 / -8.12 | +0.03 / -9.99 | +0.06 / +14.36 |
| BB_vs_BTN | +1.80 / +6.66 | -0.17 / -7.35 | +0.40 / -8.87 | +0.09 / +12.34 |
| BB_vs_SB | +2.16 / +6.18 | +0.18 / -6.51 | -0.08 / -4.17 | +0.38 / +5.78 |
| SB_vs_CO | +0.90 / +11.79 | -0.02 / -6.90 | +0.06 / -4.04 | +0.06 / +10.63 |
| SB_vs_BTN | +2.72 / +11.00 | -0.17 / -8.61 | +0.90 / -5.16 | +0.02 / +11.21 |
| BTN_vs_CO | +1.84 / +8.76 | -2.28 / -3.22 | -0.35 / -0.65 | -0.38 / +4.21 |
| LJ_vs_BB3bet | -0.34 / +0.05 | +0.34 / +1.87 | +0.03 / +2.08 | -0.16 / -0.97 |
| HJ_vs_BB3bet | -0.29 / +0.37 | +0.38 / +1.97 | +0.03 / +2.19 | -0.14 / -1.10 |
| CO_vs_BB3bet | -0.14 / +2.66 | +0.40 / +1.46 | +0.05 / +3.28 | -0.08 / -1.37 |
| BTN_vs_BB3bet | +0.01 / +4.27 | +0.09 / -0.22 | +0.12 / +6.55 | -0.01 / -2.32 |
| SB_vs_BB3bet | +0.05 / +1.49 | +0.38 / -0.26 | +0.01 / +0.44 | -0.05 / -0.89 |
| CO_vs_BTN3bet | +0.66 / +2.84 | +2.36 / +1.54 | +1.52 / +3.84 | -0.04 / -1.64 |
| BTN_vs_SB3bet | +0.03 / +4.18 | +0.08 / +0.05 | +0.31 / +6.77 | -0.01 / -2.25 |
| BB_vs_BTN4bet | -0.18 / +1.63 | +0.24 / -0.26 | -0.05 / +0.20 | +0.11 / +0.13 |
| BB_vs_SB4bet | -0.42 / +2.48 | +0.58 / -0.35 | +0.02 / -0.01 | -0.03 / +0.36 |
| SB_vs_BTN4bet | -0.20 / +2.41 | +0.17 / -0.39 | +0.02 / +0.70 | +0.10 / +0.08 |
| BTN_vs_CO4bet | +0.52 / +2.02 | +5.47 / +3.49 | +2.72 / +4.63 | -0.01 / -0.32 |



## Known weak points (read before using the ranges)

- **Tight opens, very wide defence.** The rule "continue if R * eq >= pot odds" plus PLO6's compressed
  equities (vs a 5% range, the median hand still has about 40%) makes BB defend 69 to 76% and the
  blinds' and cold-callers' ranges wide. Openers then rarely win the blinds, so RFI shrinks
  (LJ 5.5%, BTN 20%). Real PLO6 opens are much wider. The heads-up-only model, with no multiway
  pots, squeezes or postflop play, is the main cause. The players-behind factor only partly
  compensates.
- **R0 not measured** (assumption 1). Every width is sensitive to R0; see the sensitivity table.
- **Convergence is borderline, not clean.** See the convergence log. The last refinement round is
  inside tolerance, but the "two consecutive rounds" rule was not met within the caps. Some
  round-to-round noise comes from the Monte Carlo card-removal response probabilities.
- **The band mixture is biased.** Measured on the borderline classes against the actual final
  ranges, it is about -0.8 pts (exact equities are higher), with RMSE about 1.5 pts. Classes in the
  borderline band carry exact corrections; classes outside it rely on the mixture. Their decisions
  are far from a cutoff, so a 1 to 2 pt error rarely flips them, but some mid-range frequencies
  outside the band can be off.
- **Playability (z) model** is weak: noise-corrected R2 is much lower than for equity, and it
  enters R at +-30% at most.
- **Opener vs 3-bet continues with almost the whole opening range**, and BB jams over 4-bets with
  most of its 3-bet range. Both follow from R0 IP 1.0 in 3-bet pots and the tight opening ranges,
  and are probably too loose relative to a real solver.
- **BTN vs CO** 3-bets more often than it flats, because the players-behind factor applies to flats only.
- No published PLO6 solver numbers were available to reproduce here, so the external comparison
  covers first-principles identities and exact enumeration only. No numbers are quoted from memory.


## Files

| file | size |
|---|---|
| plo6_ranges_packed.json | 1.812 MB |
| widths.json | 0.002 MB |
| test_vectors.json | 0.299 MB |

Packed JSON: classes field 0.07 MB (base64; the sorted codes compress far better than the ~1.5 MB the prompt expected, and the independent decoder confirms all 962,988 codes), 24 spot fields 1.74 MB in total. Total is under the 6 MB budget.

## Resources

Elapsed wall-clock from the first stage to this report: 8.1 h on 4 vCPU (15 GB RAM).
This includes idle time and one container restart, plus failed solve attempts that were discarded
(calibrated-R0 runs, a sampler hang). CPU use was about 3.8 cores whenever a stage was running;
total CPU time is at most about 28 CPU-hours (upper bound: elapsed x 4 cores
x 0.85 average utilisation; per-process accounting was not recorded across the restart). The largest
compute items: band simulation 4.6e10 trials (64 min), borderline re-simulation about 4e10 trials
(about 90 min), model fits about 30 min, iterations about 30 min per full solve.
