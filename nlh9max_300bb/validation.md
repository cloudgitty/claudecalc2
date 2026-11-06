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
| UTG RFI | r100 | r100 | r100 | r100 | fold | fold | fold | fold | fold |
| UTG1 RFI | r100 | r100 | r100 | r100 | fold | fold | r7 | fold | fold |
| LJ RFI | r100 | r100 | r100 | r100 | fold | fold | r100 | fold | fold |
| HJ RFI | r100 | r100 | r100 | r100 | fold | fold | r100 | r26 | fold |
| CO RFI | r100 | r100 | r100 | r100 | fold | fold | r100 | r1 | fold |
| BTN RFI | r100 | r100 | r100 | r100 | fold | fold | r100 | r100 | fold |
| SB RFI | r100 | r100 | r100 | r100 | fold | fold | c100 | c2 r98 | fold |
| BB vs BTN open | r100 | r100 | r100 | r100 | c90 | fold | c1 r99 | r55 | fold |

## 3 Structure

- PASS RFI width increases UTG < UTG1 < LJ < HJ < CO < BTN: UTG 10.0 < UTG1 11.8 < LJ 14.8 < HJ 18.4 < CO 20.8 < BTN 38.2
- PASS BB defends widest against BTN and SB: vs BTN 30.3, vs SB 39.0, vs CO 19.0, vs UTG 9.6
- **FAIL** in position: 3-bet range narrower than the call range: BTN vs CO open: 3-bet 17.7, call 0.0; CO vs HJ open: 3-bet 10.8, call 0.0; BTN vs UTG open: 3-bet 6.5, call 0.0; HJ vs UTG open: 3-bet 5.9, call 0.0 - under this rake the in-position player 3-bets or folds (no flat range), so there is no call range for the 3-bets to be narrower than or polarised against
- **FAIL** in position: 3-bet range polarised (flats in the middle of the continuing range): not polarised: the continuing range is all 3-bets (linear 3-bet-or-fold)
- PASS 4-bet range narrower than the 3-bet range (% of all combos): BB 3-bet vs BTN 15.59, BTN 4-bet 9.22
- PASS 5-bet range narrower than the 3-bet range (% of all combos): BB 5-bet 3.48 vs BB 3-bet 15.59
- **FAIL** rake makes every non-blind RFI range tighter than the rake-free control: UTG 10.0 vs 10.4, UTG1 11.8 vs 11.6, LJ 14.8 vs 14.0, HJ 18.4 vs 16.3, CO 20.8 vs 22.5, BTN 38.2 vs 28.2

## 5 Exploitability reached

- preflop: 5000 iterations, per seat (bb/100): UTG 0.012, UTG1 0.020, LJ 0.017, HJ 0.017, CO 0.014, BTN 0.021, SB 0.016, BB 0.025; worst 0.025 bb/100
- PASS preflop best response <= 1.0 bb/100 per seat: worst 0.025

| Spot | flops | median exploitability % of pot | worst | max iterations |
| --- | --- | --- | --- | --- |
| 3bp_BTN_vs_BB | 100 | 0.470 | 0.499 | 320 |
| 3bp_CO_vs_BTN | 100 | 0.482 | 0.499 | 440 |
| 3bp_HJ_vs_CO | 100 | 0.477 | 0.500 | 330 |
| 3bp_SB_vs_BB | 100 | 0.473 | 0.500 | 340 |
| 3bp_UTG_vs_BB | 100 | 0.472 | 0.499 | 300 |
| 4bp_BTN_vs_BB | 100 | 0.449 | 0.498 | 170 |
| 4bp_CO_vs_BTN | 100 | 0.465 | 0.499 | 250 |
| srp_BTN_vs_BB | 100 | 0.490 | 0.500 | 640 |
| srp_CO_vs_BB | 100 | 0.490 | 0.500 | 790 |
| srp_HJ_vs_BB | 100 | 0.491 | 0.500 | 810 |
| srp_SB_vs_BB | 100 | 0.489 | 0.500 | 660 |
| srp_UTG_vs_BB | 100 | 0.490 | 0.500 | 790 |
- PASS postflop exploitability <= 0.5% of pot (or the iteration cap): worst 0.500% of pot

## 6 Test vectors

- test_vectors.json: 300 preflop and 300 postflop entries

**3 check(s) FAILED**
