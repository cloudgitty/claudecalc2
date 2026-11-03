# Validation (section 5)

Preflop solution `work/final/pre`, outputs in `.`.

## 1 Probability sums

- PASS raw strategies sum to 1 (< 1e-6): worst |sum - 1| = 1.19e-07
- PASS quantised percents sum to 100: worst deviation 0
- PASS quantisation within 5 points per action: worst |quantised - raw| = 3.33 points
- PASS decoded hand strings never exceed 100% (folds implied): 0 offenders

## 2 Round trip (independent decoder)

- PASS preflop: 100000 entries match the packer: worst frequency diff 0 points, worst EV diff 0 bb
- PASS postflop: 100001 entries match the packer

## 4 Hand sanity (frequencies in %)

| Spot | AA | KK | AKs | AKo | 22 | 87s | A5s | K9o | 72o |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UTG RFI | r100 | r100 | r100 | r100 | fold | fold | r7 | fold | fold |
| UTG1 RFI | r100 | r100 | r100 | r100 | fold | fold | r22 | r9 | fold |
| LJ RFI | r100 | r100 | r100 | r100 | fold | fold | r36 | fold | fold |
| HJ RFI | r100 | r100 | r100 | r100 | fold | fold | r96 | r4 | fold |
| CO RFI | r100 | r100 | r100 | r100 | fold | fold | r99 | r97 | fold |
| BTN RFI | r100 | r100 | r100 | r100 | fold | fold | r100 | r100 | fold |
| SB RFI | r100 | r100 | r100 | r100 | r60 | r66 | c100 | c98 r2 | fold |
| BB vs BTN open | r100 | r100 | r100 | r100 | r3 | fold | r87 | r32 | fold |

## 3 Structure

- PASS RFI width increases UTG < UTG1 < LJ < HJ < CO < BTN: UTG 9.3 < UTG1 11.5 < LJ 14.7 < HJ 16.2 < CO 25.1 < BTN 37.7
- PASS BB defends widest against BTN and SB: vs BTN 22.8, vs SB 36.7, vs CO 17.6, vs UTG 10.1
- **FAIL** in position: 3-bet range narrower than the call range: BTN vs CO open: 3-bet 12.5, call 0.0; CO vs HJ open: 3-bet 10.8, call 0.0; BTN vs UTG open: 3-bet 6.6, call 0.0; HJ vs UTG open: 3-bet 6.1, call 0.0 - under this rake the in-position player 3-bets or folds (no flat range), so there is no call range for the 3-bets to be narrower than or polarised against
- **FAIL** in position: 3-bet range polarised (flats in the middle of the continuing range): not polarised: the continuing range is all 3-bets (linear 3-bet-or-fold)
- PASS 4-bet range narrower than the 3-bet range (% of all combos): BB 3-bet vs BTN 19.55, BTN 4-bet 8.83
- PASS 5-bet range narrower than the 3-bet range (% of all combos): BB 5-bet 5.04 vs BB 3-bet 19.55
- **FAIL** rake makes every non-blind RFI range tighter than the rake-free control: UTG 9.3 vs 9.5, UTG1 11.5 vs 12.1, LJ 14.7 vs 13.8, HJ 16.2 vs 18.4, CO 25.1 vs 26.4, BTN 37.7 vs 45.3

## 5 Exploitability reached

- preflop: 5000 iterations, per seat (bb/100): UTG 0.007, UTG1 0.113, LJ 0.031, HJ 0.045, CO 0.058, BTN 0.038, SB 0.048, BB 0.052; worst 0.113 bb/100
- PASS preflop best response <= 1.0 bb/100 per seat: worst 0.113

| Spot | flops | median exploitability % of pot | worst | max iterations |
| --- | --- | --- | --- | --- |
| 3bp_BTN_vs_BB | 100 | 0.454 | 0.500 | 150 |
| 3bp_CO_vs_BTN | 100 | 0.461 | 0.500 | 220 |
| 3bp_HJ_vs_CO | 100 | 0.466 | 0.500 | 240 |
| 3bp_SB_vs_BB | 100 | 0.450 | 0.498 | 510 |
| 3bp_UTG_vs_BB | 100 | 0.452 | 0.500 | 160 |
| 4bp_BTN_vs_BB | 100 | 0.384 | 0.497 | 80 |
| srp_BTN_vs_BB | 100 | 0.475 | 0.500 | 440 |
| srp_CO_vs_BB | 100 | 0.477 | 0.500 | 320 |
| srp_HJ_vs_BB | 100 | 0.479 | 0.500 | 410 |
| srp_SB_vs_BB | 100 | 0.475 | 0.500 | 310 |
| srp_UTG_vs_BB | 100 | 0.480 | 0.499 | 480 |
- PASS postflop exploitability <= 0.5% of pot (or the iteration cap): worst 0.500% of pot

## 6 Test vectors

- test_vectors.json: 300 preflop and 300 postflop entries

**3 check(s) FAILED**
