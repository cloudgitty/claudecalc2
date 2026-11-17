# Validation (section 5)

Preflop solution `work2/polish2/pre`, outputs in `.`.

## 1 Probability sums

- PASS raw strategies sum to 1 (< 1e-6): worst |sum - 1| = 1.19e-07
- PASS quantised percents sum to 100: worst deviation 0
- PASS quantisation within 5 points per action: worst |quantised - raw| = 3.33 points
- PASS decoded hand strings never exceed 100% (folds implied): 0 offenders

## 2 Round trip (independent decoder)

- PASS preflop: 100000 entries match the packer: worst frequency diff 0 points, worst EV diff 0 bb
- PASS postflop: 100002 entries match the packer

## 4 Hand sanity (frequencies in %)

| Spot | AA | KK | AKs | AKo | 22 | 87s | A5s | K9o | 72o |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UTG RFI | r100 | r100 | r100 | r100 | fold | fold | r5 | fold | fold |
| UTG1 RFI | r100 | r100 | r100 | r100 | fold | r5 | r8 | r1 | fold |
| LJ RFI | r100 | r100 | r100 | r100 | fold | r14 | r2 | r2 | fold |
| HJ RFI | r100 | r100 | r100 | r100 | fold | r17 | r40 | r6 | fold |
| CO RFI | r100 | r100 | r100 | r100 | r92 | r46 | r94 | r2 | r2 |
| BTN RFI | r100 | r100 | r100 | r100 | r100 | r94 | r100 | r11 | r24 |
| SB RFI | r100 | r100 | r100 | c2 r98 | r100 | r1 | r100 | r91 | fold |
| BB vs BTN open | r100 | r100 | r100 | r100 | fold | r98 | r9 | r6 | fold |

## 3 Structure

- PASS RFI width increases UTG < UTG1 < LJ < HJ < CO < BTN: UTG 8.1 < UTG1 9.8 < LJ 12.8 < HJ 16.7 < CO 26.3 < BTN 68.6
- PASS BB defends widest against BTN and SB: vs BTN 18.8, vs SB 42.5, vs CO 10.2, vs UTG 2.9
- **FAIL** in position: 3-bet range narrower than the call range: BTN vs CO open: 3-bet 19.3, call 0.0; CO vs HJ open: 3-bet 13.4, call 0.0; BTN vs UTG open: 3-bet 7.4, call 0.1; HJ vs UTG open: 3-bet 6.5, call 0.0 - under this rake the in-position player 3-bets or folds (no flat range), so there is no call range for the 3-bets to be narrower than or polarised against
- **FAIL** in position: 3-bet range polarised (flats in the middle of the continuing range): not polarised: the continuing range is all 3-bets (linear 3-bet-or-fold)
- PASS 4-bet range narrower than the 3-bet range (% of all combos): BB 3-bet vs BTN 18.83, BTN 4-bet 8.18
- PASS 5-bet range narrower than the 3-bet range (% of all combos): BB 5-bet 6.57 vs BB 3-bet 18.83
- PASS rake makes every non-blind RFI range tighter than the rake-free control: UTG 8.1 vs 9.2, UTG1 9.8 vs 10.2, LJ 12.8 vs 13.0, HJ 16.7 vs 16.8, CO 26.3 vs 31.4, BTN 68.6 vs 72.4

## 5 Exploitability reached

- preflop: 25000 iterations, per seat (bb/100): UTG 0.071, UTG1 0.056, LJ 0.076, HJ 0.089, CO 0.085, BTN 0.226, SB 0.228, BB 0.229; worst 0.229 bb/100
- PASS preflop best response <= 1.0 bb/100 per seat: worst 0.229

| Spot | flops | median exploitability % of pot | worst | max iterations |
| --- | --- | --- | --- | --- |
| 3bp_BTN_vs_BB | 100 | 0.467 | 0.499 | 280 |
| 3bp_CO_vs_BTN | 100 | 0.474 | 0.898 | 340 |
| 3bp_HJ_vs_CO | 100 | 0.472 | 0.500 | 410 |
| 3bp_SB_vs_BB | 100 | 0.469 | 0.499 | 340 |
| 3bp_UTG_vs_BB | 100 | 0.470 | 0.500 | 310 |
| 4bp_BTN_vs_BB | 100 | 0.439 | 0.496 | 150 |
| 4bp_CO_vs_BTN | 100 | 0.457 | 0.500 | 190 |
| srp_BTN_vs_BB | 100 | 0.489 | 0.500 | 550 |
| srp_BTN_vs_SB | 100 | 0.480 | 0.500 | 520 |
| srp_CO_vs_BB | 100 | 0.489 | 0.500 | 590 |
| srp_HJ_vs_BB | 100 | 0.487 | 0.499 | 590 |
| srp_SB_vs_BB | 100 | 0.484 | 0.499 | 430 |
| srp_UTG_vs_BB | 100 | 0.486 | 0.500 | 510 |
| srp_UTG_vs_BTN | 100 | 0.483 | 0.499 | 670 |
- **FAIL** postflop exploitability <= 0.5% of pot (or the iteration cap): worst 0.898% of pot

## 6 Test vectors

- test_vectors.json: 300 preflop and 300 postflop entries

**3 check(s) FAILED**

## 7 Improvement-plan checks

- **FAIL** payoff conservation (rake 0, R = 1, 300 iterations): sum of the 8 seats' EVs -0.0222 bb per hand (tolerance 0.02; the original model gave +0.066 at 100bb)
- **FAIL** convergence of `main`: 10 rounds, largest RFI moves 15.47, 6.01, 5.81, 4.58, 2.55, 0.98, 0.75, 1.27, 1.97, 4.44
- **FAIL** convergence of `rake0`: 10 rounds, largest RFI moves 25.54, 7.27, 5.17, 5.05, 4.27, 2.89, 2.56, 1.83, 1.79, 0.55
- **FAIL** convergence of `cap3`: 10 rounds, largest RFI moves 16.19, 7.94, 4.07, 3.92, 0.64, 1.75, 1.53, 2.99, 1.03, 1.24
- **FAIL** convergence of `pct5`: 10 rounds, largest RFI moves 17.34, 9.80, 1.62, 4.34, 1.30, 3.12, 4.67, 0.78, 2.66, 1.68
  - 3bp_CO_vs_BTN CO: mean correction 0.872, buckets 1.01 / 0.82 / 0.78 / 0.87 / 0.83; realised share turn-end 0.458 river 0.401
  - 3bp_CO_vs_BTN BTN: mean correction 1.163, buckets 1.21 / 1.32 / 1.13 / 1.09 / 1.11; realised share turn-end 0.417 river 0.471
  - srp_BTN_vs_BB BB: mean correction 0.830, buckets 0.74 / 0.74 / 0.99 / 0.92 / 0.84; realised share turn-end 0.377 river 0.307
  - srp_BTN_vs_BB BTN: mean correction 1.171, buckets 1.27 / 1.39 / 1.01 / 1.07 / 1.09; realised share turn-end 0.440 river 0.477
- river error on the root strategies: mean 27.8 points, worst node 60.1 points of frequency moved between the turn-end and the river solves (30 first decisions: OOP root and IP after a check); the stored strategies are turn-end solves
