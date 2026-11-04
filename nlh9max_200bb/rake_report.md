# Effect of the rake (output C)

Widths are % of all 1,326 combos. Every column is a full re-solve of the same tree,
same sizes and same iteration count; only the rake parameters differ.

| Measure | rake 10% / 5bb | rake 0 (control) | cap 3bb | rake 5% |
| --- | --- | --- | --- | --- |
| RFI UTG | 11.4 | 12.1 | 11.1 | 12.4 |
| RFI UTG1 | 12.7 | 12.9 | 12.6 | 12.9 |
| RFI LJ | 14.8 | 14.7 | 17.2 | 14.8 |
| RFI HJ | 19.3 | 18.1 | 20.3 | 19.9 |
| RFI CO | 24.5 | 22.6 | 23.9 | 24.8 |
| RFI BTN | 35.3 | 34.0 | 36.3 | 35.0 |
| RFI SB | 51.5 | 45.3 | 51.8 | 48.5 |
| SB limp | 3.8 | 14.1 | 2.9 | 15.3 |
| BB vs BTN open: call | 3.2 | 13.5 | 1.7 | 11.6 |
| BB vs BTN open: 3-bet | 18.7 | 16.4 | 19.5 | 16.6 |
| BB vs SB open: call | 0.3 | 9.5 | 0.9 | 0.0 |
| BB vs SB open: 3-bet | 33.0 | 29.5 | 32.7 | 32.8 |
| BB vs CO open: call | 5.9 | 27.2 | 10.1 | 13.8 |
| BB vs CO open: 3-bet | 9.8 | 8.1 | 9.7 | 9.7 |
| BB vs UTG open: call | 7.9 | 6.7 | 7.9 | 7.3 |
| BB vs UTG open: 3-bet | 3.5 | 4.4 | 5.4 | 4.0 |
| BTN vs BB 3-bet: call | 1.4 | 7.7 | 0.7 | 5.5 |
| BTN vs BB 3-bet: 4-bet | 24.1 | 22.3 | 24.6 | 21.9 |

| Postflop measure | rake 10% / 5bb | rake 0 (control) | cap 3bb | rake 5% |
| --- | --- | --- | --- | --- |
| 3bp_BTN_vs_BB c-bet % (preflop raiser), dry AcJc8d | 31.9 | - | 0.9 | 2.9 |
| 3bp_BTN_vs_BB c-bet % (preflop raiser), wet 9c7c6d | 96.8 | - | 91.8 | 17.2 |
| srp_BTN_vs_BB c-bet % (preflop raiser), dry AcJc8d | 31.0 | 41.0 | - | 55.0 |
| srp_BTN_vs_BB c-bet % (preflop raiser), wet 9c7c6d | 18.6 | 52.6 | - | 89.6 |

## Overall

- rake 10% / 5bb: sum of the 7 RFI widths 169.5 points (UTG 11.4 ... BTN 35.3), SB limp 3.8
- rake 0 (control): sum of the 7 RFI widths 159.6 points (UTG 12.1 ... BTN 34.0), SB limp 14.1
- cap 3bb: sum of the 7 RFI widths 173.2 points (UTG 11.1 ... BTN 36.3), SB limp 2.9
- rake 5%: sum of the 7 RFI widths 168.2 points (UTG 12.4 ... BTN 35.0), SB limp 15.3
- rake 10%/5bb against the rake-free control: RFI change per seat UTG -0.7, UTG1 -0.2, LJ +0.1, HJ +1.3, CO +1.9, BTN +1.3, SB +6.2
