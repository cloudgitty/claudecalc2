# Effect of the rake (output C)

Widths are % of all 1,326 combos. Every column is a full re-solve of the same tree,
same sizes and same iteration count; only the rake parameters differ.

| Measure | rake 10% / 5bb | rake 0 (control) | cap 3bb | rake 5% |
| --- | --- | --- | --- | --- |
| RFI UTG | 9.3 | 9.5 | 9.3 | 9.3 |
| RFI UTG1 | 11.5 | 12.1 | 11.8 | 11.9 |
| RFI LJ | 14.7 | 13.8 | 14.4 | 14.4 |
| RFI HJ | 16.2 | 18.4 | 17.9 | 18.0 |
| RFI CO | 25.1 | 26.4 | 26.2 | 26.2 |
| RFI BTN | 37.7 | 45.3 | 44.9 | 45.7 |
| RFI SB | 46.1 | 35.6 | 49.8 | 45.0 |
| SB limp | 4.1 | 12.2 | 0.9 | 3.0 |
| BB vs BTN open: call | 3.3 | 2.5 | 0.0 | 0.3 |
| BB vs BTN open: 3-bet | 19.5 | 18.3 | 19.7 | 19.4 |
| BB vs SB open: call | 8.6 | 23.0 | 2.4 | 4.9 |
| BB vs SB open: 3-bet | 28.1 | 23.6 | 31.7 | 30.4 |
| BB vs CO open: call | 7.1 | 15.7 | 4.1 | 7.7 |
| BB vs CO open: 3-bet | 10.5 | 9.7 | 10.3 | 9.9 |
| BB vs UTG open: call | 6.0 | 11.9 | 4.2 | 7.7 |
| BB vs UTG open: 3-bet | 4.1 | 3.1 | 4.1 | 3.0 |
| BTN vs BB 3-bet: call | 2.4 | 10.2 | 2.4 | 2.6 |
| BTN vs BB 3-bet: 4-bet | 23.4 | 21.0 | 23.7 | 23.7 |

| Postflop measure | rake 10% / 5bb | rake 0 (control) | cap 3bb | rake 5% |
| --- | --- | --- | --- | --- |
| 3bp_BTN_vs_BB c-bet % (preflop raiser), dry AcJc8d | 0.6 | 0.3 | 0.3 | 0.4 |
| 3bp_BTN_vs_BB c-bet % (preflop raiser), wet 9c7c6d | 91.9 | 91.9 | 91.9 | 91.9 |
| srp_BTN_vs_BB c-bet % (preflop raiser), dry AcJc8d | 24.9 | 24.2 | 26.2 | 22.8 |
| srp_BTN_vs_BB c-bet % (preflop raiser), wet 9c7c6d | 65.2 | 56.2 | 64.2 | 60.0 |

## Overall

- rake 10% / 5bb: sum of the 7 RFI widths 160.6 points (UTG 9.3 ... BTN 37.7), SB limp 4.1
- rake 0 (control): sum of the 7 RFI widths 161.0 points (UTG 9.5 ... BTN 45.3), SB limp 12.2
- cap 3bb: sum of the 7 RFI widths 174.2 points (UTG 9.3 ... BTN 44.9), SB limp 0.9
- rake 5%: sum of the 7 RFI widths 170.5 points (UTG 9.3 ... BTN 45.7), SB limp 3.0
- rake 10%/5bb against the rake-free control: RFI change per seat UTG -0.2, UTG1 -0.6, LJ +1.0, HJ -2.2, CO -1.3, BTN -7.7, SB +10.5
