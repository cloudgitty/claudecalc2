# Effect of the rake (output C)

Widths are % of all 1,326 combos. Every column is a full re-solve of the same tree,
same sizes and same iteration count; only the rake parameters differ.

| Measure | rake 10% / 5bb | rake 0 (control) | cap 3bb | rake 5% |
| --- | --- | --- | --- | --- |
| RFI UTG | 11.4 | 12.0 | 11.5 | 11.7 |
| RFI UTG1 | 12.7 | 14.1 | 13.9 | 14.0 |
| RFI LJ | 14.8 | 17.3 | 17.8 | 16.9 |
| RFI HJ | 19.3 | 21.5 | 22.1 | 21.3 |
| RFI CO | 24.5 | 19.2 | 23.9 | 21.8 |
| RFI BTN | 35.3 | 29.6 | 31.4 | 31.5 |
| RFI SB | 51.5 | 50.5 | 55.8 | 51.1 |
| SB limp | 3.8 | 17.1 | 1.3 | 4.8 |
| BB vs BTN open: call | 3.2 | 6.3 | 0.2 | 2.8 |
| BB vs BTN open: 3-bet | 18.7 | 20.7 | 22.0 | 21.1 |
| BB vs SB open: call | 0.3 | 0.0 | 0.0 | 0.0 |
| BB vs SB open: 3-bet | 33.0 | 32.6 | 33.0 | 32.9 |
| BB vs CO open: call | 5.9 | 28.7 | 11.6 | 17.6 |
| BB vs CO open: 3-bet | 9.8 | 8.3 | 9.9 | 9.0 |
| BB vs UTG open: call | 7.9 | 9.8 | 5.6 | 7.6 |
| BB vs UTG open: 3-bet | 3.5 | 4.3 | 4.7 | 4.8 |
| BTN vs BB 3-bet: call | 1.4 | 0.0 | 0.0 | 0.0 |
| BTN vs BB 3-bet: 4-bet | 24.1 | 25.4 | 25.1 | 25.0 |

| Postflop measure | rake 10% / 5bb | rake 0 (control) | cap 3bb | rake 5% |
| --- | --- | --- | --- | --- |
| 3bp_BTN_vs_BB c-bet % (preflop raiser), dry AcJc8d | 31.9 | 36.4 | 34.2 | 34.4 |
| 3bp_BTN_vs_BB c-bet % (preflop raiser), wet 9c7c6d | 96.8 | 96.0 | 96.5 | 96.8 |
| srp_BTN_vs_BB c-bet % (preflop raiser), dry AcJc8d | 31.0 | 37.1 | 32.1 | 33.0 |
| srp_BTN_vs_BB c-bet % (preflop raiser), wet 9c7c6d | 18.6 | 24.0 | 19.8 | 21.8 |

## Overall

- rake 10% / 5bb: sum of the 7 RFI widths 169.5 points (UTG 11.4 ... BTN 35.3), SB limp 3.8
- rake 0 (control): sum of the 7 RFI widths 164.1 points (UTG 12.0 ... BTN 29.6), SB limp 17.1
- cap 3bb: sum of the 7 RFI widths 176.3 points (UTG 11.5 ... BTN 31.4), SB limp 1.3
- rake 5%: sum of the 7 RFI widths 168.2 points (UTG 11.7 ... BTN 31.5), SB limp 4.8
- rake 10%/5bb against the rake-free control: RFI change per seat UTG -0.6, UTG1 -1.4, LJ -2.5, HJ -2.1, CO +5.3, BTN +5.7, SB +1.0
