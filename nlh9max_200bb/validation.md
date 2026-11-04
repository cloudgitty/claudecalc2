# Validation (section 5)

Preflop solution `work/final/pre`, outputs in `.`.

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
| UTG RFI | r100 | r100 | r100 | r100 | fold | fold | r100 | fold | fold |
| UTG1 RFI | r100 | r100 | r100 | r100 | fold | fold | r69 | fold | fold |
| LJ RFI | r100 | r100 | r100 | r100 | fold | fold | r49 | fold | fold |
| HJ RFI | r100 | r100 | r100 | r100 | fold | fold | r100 | r26 | fold |
| CO RFI | r100 | r100 | r100 | r100 | fold | fold | r100 | r95 | fold |
| BTN RFI | r100 | r100 | r100 | r100 | fold | fold | r100 | r100 | fold |
| SB RFI | r100 | r100 | r100 | r100 | r2 | fold | c1 r99 | c6 r94 | fold |
| BB vs BTN open | r100 | r100 | r100 | r100 | fold | fold | r6 | r14 | fold |

## 3 Structure

- PASS RFI width increases UTG < UTG1 < LJ < HJ < CO < BTN: UTG 11.4 < UTG1 12.7 < LJ 14.8 < HJ 19.3 < CO 24.5 < BTN 35.3
- PASS BB defends widest against BTN and SB: vs BTN 21.9, vs SB 33.3, vs CO 15.7, vs UTG 11.3
- **FAIL** in position: 3-bet range narrower than the call range: BTN vs CO open: 3-bet 15.3, call 0.0; CO vs HJ open: 3-bet 9.8, call 0.0; BTN vs UTG open: 3-bet 6.1, call 0.0; HJ vs UTG open: 3-bet 5.8, call 0.0 - under this rake the in-position player 3-bets or folds (no flat range), so there is no call range for the 3-bets to be narrower than or polarised against
- **FAIL** in position: 3-bet range polarised (flats in the middle of the continuing range): not polarised: the continuing range is all 3-bets (linear 3-bet-or-fold)
- PASS 4-bet range narrower than the 3-bet range (% of all combos): BB 3-bet vs BTN 18.72, BTN 4-bet 8.52
- PASS 5-bet range narrower than the 3-bet range (% of all combos): BB 5-bet 5.43 vs BB 3-bet 18.72
- **FAIL** rake makes every non-blind RFI range tighter than the rake-free control: UTG 11.4 vs 12.1, UTG1 12.7 vs 12.9, LJ 14.8 vs 14.7, HJ 19.3 vs 18.1, CO 24.5 vs 22.6, BTN 35.3 vs 34.0

## 5 Exploitability reached

- preflop: 5000 iterations, per seat (bb/100): UTG 0.013, UTG1 0.021, LJ 0.024, HJ 0.032, CO 0.043, BTN 0.038, SB 0.041, BB 0.040; worst 0.043 bb/100
- PASS preflop best response <= 1.0 bb/100 per seat: worst 0.043

| Spot | flops | median exploitability % of pot | worst | max iterations |
| --- | --- | --- | --- | --- |
| 3bp_BTN_vs_BB | 100 | 0.464 | 0.500 | 240 |
| 3bp_CO_vs_BTN | 100 | 0.477 | 0.499 | 450 |
| 3bp_HJ_vs_CO | 100 | 0.477 | 0.500 | 320 |
| 3bp_SB_vs_BB | 100 | 0.465 | 0.498 | 320 |
| 3bp_UTG_vs_BB | 100 | 0.471 | 0.500 | 310 |
| 4bp_BTN_vs_BB | 100 | 0.447 | 0.499 | 140 |
| 4bp_CO_vs_BTN | 100 | 0.464 | 0.499 | 200 |
| srp_BTN_vs_BB | 100 | 0.486 | 0.499 | 600 |
| srp_CO_vs_BB | 100 | 0.488 | 0.500 | 550 |
| srp_HJ_vs_BB | 100 | 0.487 | 0.500 | 670 |
| srp_SB_vs_BB | 100 | 0.487 | 0.802 | 730 |
| srp_UTG_vs_BB | 100 | 0.487 | 0.500 | 610 |
- **FAIL** postflop exploitability <= 0.5% of pot (or the iteration cap): worst 0.802% of pot

## 6 Test vectors

- test_vectors.json: 300 preflop and 300 postflop entries

**4 check(s) FAILED**
