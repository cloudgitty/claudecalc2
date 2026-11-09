# Validation (section 5)

Preflop solution `work2/polish2/pre`, outputs in `.`.

## 1 Probability sums

- PASS raw strategies sum to 1 (< 1e-6): worst |sum - 1| = 1.19e-07
- PASS quantised percents sum to 100: worst deviation 0
- PASS quantisation within 5 points per action: worst |quantised - raw| = 3.33 points
- PASS decoded hand strings never exceed 100% (folds implied): 0 offenders

## 2 Round trip (independent decoder)

- PASS preflop: 100000 entries match the packer: worst frequency diff 0 points, worst EV diff 0 bb
- PASS postflop: 100008 entries match the packer

## 4 Hand sanity (frequencies in %)

| Spot | AA | KK | AKs | AKo | 22 | 87s | A5s | K9o | 72o |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UTG RFI | r100 | r100 | r100 | r100 | r9 | fold | r42 | fold | fold |
| UTG1 RFI | r100 | r100 | r100 | r100 | r13 | fold | r45 | fold | fold |
| LJ RFI | r100 | r100 | r100 | r100 | r23 | r6 | r44 | fold | fold |
| HJ RFI | r100 | r100 | r100 | r100 | r15 | fold | r86 | r32 | fold |
| CO RFI | r100 | r100 | r100 | r100 | r66 | r11 | r38 | r36 | fold |
| BTN RFI | r100 | r100 | r100 | r100 | r96 | r100 | r100 | r2 | fold |
| SB RFI | r100 | r100 | r100 | r100 | r48 | r52 | r100 | r75 | fold |
| BB vs BTN open | r100 | r100 | r100 | r100 | r5 | r74 | r8 | r5 | fold |

## 3 Structure

- PASS RFI width increases UTG < UTG1 < LJ < HJ < CO < BTN: UTG 9.1 < UTG1 11.8 < LJ 14.2 < HJ 18.2 < CO 26.3 < BTN 45.7
- PASS BB defends widest against BTN and SB: vs BTN 22.0, vs SB 35.1, vs CO 11.1, vs UTG 4.6
- **FAIL** in position: 3-bet range narrower than the call range: BTN vs CO open: 3-bet 16.2, call 0.0; CO vs HJ open: 3-bet 11.0, call 0.0; BTN vs UTG open: 3-bet 6.5, call 0.0; HJ vs UTG open: 3-bet 6.0, call 0.0 - under this rake the in-position player 3-bets or folds (no flat range), so there is no call range for the 3-bets to be narrower than or polarised against
- **FAIL** in position: 3-bet range polarised (flats in the middle of the continuing range): not polarised: the continuing range is all 3-bets (linear 3-bet-or-fold)
- PASS 4-bet range narrower than the 3-bet range (% of all combos): BB 3-bet vs BTN 18.84, BTN 4-bet 11.11
- PASS 5-bet range narrower than the 3-bet range (% of all combos): BB 5-bet 7.66 vs BB 3-bet 18.84
- **FAIL** rake makes every non-blind RFI range tighter than the rake-free control: UTG 9.1 vs 9.5, UTG1 11.8 vs 11.1, LJ 14.2 vs 13.2, HJ 18.2 vs 17.2, CO 26.3 vs 26.6, BTN 45.7 vs 53.9

## 5 Exploitability reached

- preflop: 25000 iterations, per seat (bb/100): UTG 0.049, UTG1 0.031, LJ 0.056, HJ 0.055, CO 0.055, BTN 0.081, SB 0.031, BB 0.038; worst 0.081 bb/100
- PASS preflop best response <= 1.0 bb/100 per seat: worst 0.081

| Spot | flops | median exploitability % of pot | worst | max iterations |
| --- | --- | --- | --- | --- |
| 3bp_CO_vs_BTN | 100 | 0.461 | 0.499 | 250 |
| 3bp_HJ_vs_CO | 100 | 0.457 | 0.498 | 210 |
| 3bp_SB_vs_BB | 100 | 0.469 | 0.499 | 310 |
| 3bp_UTG_vs_BB | 100 | 0.451 | 0.498 | 360 |
| 4bp_CO_vs_BTN | 100 | 0.427 | 0.499 | 90 |
| srp_BTN_vs_BB | 100 | 0.477 | 0.500 | 380 |
| srp_SB_vs_BB | 100 | 0.474 | 0.499 | 300 |
| srp_UTG_vs_BB | 100 | 0.475 | 0.955 | 610 |
| srp_UTG_vs_BTN | 100 | 0.469 | 0.635 | 500 |
- **FAIL** postflop exploitability <= 0.5% of pot (or the iteration cap): worst 0.955% of pot

## 6 Test vectors

- test_vectors.json: 300 preflop and 300 postflop entries

**4 check(s) FAILED**

## 7 Improvement-plan checks

- **FAIL** payoff conservation (rake 0, R = 1, 300 iterations): sum of the 8 seats' EVs -0.0227 bb per hand (tolerance 0.02; the original model gave +0.066 at 100bb)
- **FAIL** convergence of `main`: 10 rounds, largest RFI moves 4.13, 1.62, 1.37, 1.11, 0.62, 1.10, 0.62, 0.61, 0.87, 0.40
- **FAIL** convergence of `rake0`: 10 rounds, largest RFI moves 3.39, 2.90, 3.11, 1.63, 1.91, 1.71, 1.11, 0.87, 1.18, 1.10
- **FAIL** convergence of `cap3`: 10 rounds, largest RFI moves 1.16, 2.29, 0.55, 2.96, 0.70, 1.12, 0.62, 1.44, 1.62, 1.11
- **FAIL** convergence of `pct5`: 10 rounds, largest RFI moves 1.01, 1.76, 1.63, 1.86, 3.12, 0.62, 3.15, 2.49, 0.28, 1.96
  - 3bp_CO_vs_BTN CO: mean correction 0.833, buckets 0.96 / 0.80 / 1.00 / 1.00 / 1.05; realised share turn-end 0.324 river 0.286
  - 3bp_CO_vs_BTN BTN: mean correction 1.199, buckets 1.70 / 1.23 / 1.17 / 0.91 / 0.98; realised share turn-end 0.520 river 0.560
  - srp_BTN_vs_BB BB: mean correction 0.770, buckets 0.67 / 1.00 / 1.00 / 1.04 / 1.15; realised share turn-end 0.525 river 0.401
  - srp_BTN_vs_BB BTN: mean correction 1.353, buckets 1.41 / 1.31 / 1.41 / 1.41 / 1.22; realised share turn-end 0.354 river 0.451
  - srp_SB_vs_BB SB: mean correction 0.877, buckets 0.64 / 0.92 / 0.93 / 0.85 / 1.07; realised share turn-end 0.375 river 0.343
  - srp_SB_vs_BB BB: mean correction 1.054, buckets 0.92 / 1.10 / 0.93 / 1.35 / 1.02; realised share turn-end 0.450 river 0.462
- river error on the root strategies: mean 25.1 points, worst node 73.8 points of frequency moved between the turn-end and the river solves (30 first decisions: OOP root and IP after a check); the stored strategies are turn-end solves
