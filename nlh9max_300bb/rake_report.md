# Effect of the rake (output C)

Widths are % of all 1,326 combos. Every column is a full re-solve of the same tree,
same sizes and same iteration count; only the rake parameters differ.

| Measure | rake 10% / 5bb | rake 0 (control) | cap 3bb | rake 5% |
| --- | --- | --- | --- | --- |
| RFI UTG | 10.0 | 10.4 | 10.8 | 10.8 |
| RFI UTG1 | 11.8 | 11.6 | 13.4 | 11.8 |
| RFI LJ | 14.8 | 14.0 | 15.1 | 14.3 |
| RFI HJ | 18.4 | 16.3 | 19.5 | 18.3 |
| RFI CO | 20.8 | 22.5 | 19.7 | 20.2 |
| RFI BTN | 38.2 | 28.2 | 32.7 | 32.4 |
| RFI SB | 38.7 | 50.7 | 47.3 | 35.0 |
| SB limp | 15.8 | 15.3 | 17.9 | 28.9 |
| BB vs BTN open: call | 14.7 | 60.4 | 20.5 | 28.1 |
| BB vs BTN open: 3-bet | 15.6 | 10.6 | 13.9 | 12.9 |
| BB vs SB open: call | 13.2 | 33.0 | 18.1 | 27.3 |
| BB vs SB open: 3-bet | 25.8 | 20.0 | 23.6 | 20.1 |
| BB vs CO open: call | 12.5 | 37.0 | 11.8 | 25.8 |
| BB vs CO open: 3-bet | 6.5 | 6.6 | 9.0 | 6.4 |
| BB vs UTG open: call | 5.7 | 23.4 | 10.4 | 7.1 |
| BB vs UTG open: 3-bet | 3.9 | 2.4 | 2.3 | 3.5 |
| BTN vs BB 3-bet: call | 0.9 | 9.6 | 2.9 | 6.1 |
| BTN vs BB 3-bet: 4-bet | 24.1 | 19.2 | 23.0 | 20.8 |

| Postflop measure | rake 10% / 5bb | rake 0 (control) | cap 3bb | rake 5% |
| --- | --- | --- | --- | --- |
| 3bp_BTN_vs_BB c-bet % (preflop raiser), dry AcJc8d | 10.9 | 3.5 | 7.2 | 15.3 |
| 3bp_BTN_vs_BB c-bet % (preflop raiser), wet 9c7c6d | 83.5 | 36.7 | 56.7 | 14.2 |
| srp_BTN_vs_BB c-bet % (preflop raiser), dry AcJc8d | 41.5 | 40.6 | 43.3 | 37.6 |
| srp_BTN_vs_BB c-bet % (preflop raiser), wet 9c7c6d | 44.8 | 45.9 | 50.2 | 44.9 |

## Overall

- rake 10% / 5bb: sum of the 7 RFI widths 152.7 points (UTG 10.0 ... BTN 38.2), SB limp 15.8
- rake 0 (control): sum of the 7 RFI widths 153.8 points (UTG 10.4 ... BTN 28.2), SB limp 15.3
- cap 3bb: sum of the 7 RFI widths 158.6 points (UTG 10.8 ... BTN 32.7), SB limp 17.9
- rake 5%: sum of the 7 RFI widths 142.7 points (UTG 10.8 ... BTN 32.4), SB limp 28.9
- rake 10%/5bb against the rake-free control: RFI change per seat UTG -0.5, UTG1 +0.2, LJ +0.8, HJ +2.1, CO -1.7, BTN +10.0, SB -12.0
