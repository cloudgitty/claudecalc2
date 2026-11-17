# Effect of the rake (output C)

Widths are % of all 1,326 combos. Every column is a full re-solve of the same tree,
same sizes and same iteration count; only the rake parameters differ.

| Measure | rake 10% / 5bb | rake 0 (control) | cap 3bb | rake 5% |
| --- | --- | --- | --- | --- |
| RFI UTG | 8.1 | 9.2 | 8.0 | 9.0 |
| RFI UTG1 | 9.8 | 10.2 | 8.9 | 9.7 |
| RFI LJ | 12.8 | 13.0 | 11.6 | 12.4 |
| RFI HJ | 16.7 | 16.8 | 15.5 | 16.1 |
| RFI CO | 26.3 | 31.4 | 30.4 | 28.2 |
| RFI BTN | 68.6 | 72.4 | 69.6 | 67.0 |
| RFI SB | 29.1 | 18.2 | 24.6 | 23.7 |
| SB limp | 0.4 | 18.6 | 2.9 | 4.5 |
| BB vs BTN open: call | 0.0 | 23.7 | 6.0 | 10.0 |
| BB vs BTN open: 3-bet | 18.8 | 13.4 | 16.8 | 14.8 |
| BB vs SB open: call | 18.9 | 60.0 | 16.6 | 38.7 |
| BB vs SB open: 3-bet | 23.6 | 12.0 | 25.0 | 15.5 |
| BB vs CO open: call | 0.0 | 1.1 | 0.0 | 0.0 |
| BB vs CO open: 3-bet | 10.2 | 10.0 | 9.0 | 8.9 |
| BB vs UTG open: call | 0.0 | 0.0 | 0.0 | 0.0 |
| BB vs UTG open: 3-bet | 2.9 | 3.1 | 2.9 | 3.9 |
| BTN vs BB 3-bet: call | 24.2 | 50.8 | 34.2 | 34.1 |
| BTN vs BB 3-bet: 4-bet | 11.9 | 6.7 | 9.4 | 9.3 |

## Overall

- rake 10% / 5bb: sum of the 7 RFI widths 171.4 points (UTG 8.1 ... BTN 68.6), SB limp 0.4
- rake 0 (control): sum of the 7 RFI widths 171.2 points (UTG 9.2 ... BTN 72.4), SB limp 18.6
- cap 3bb: sum of the 7 RFI widths 168.6 points (UTG 8.0 ... BTN 69.6), SB limp 2.9
- rake 5%: sum of the 7 RFI widths 166.2 points (UTG 9.0 ... BTN 67.0), SB limp 4.5
- rake 10%/5bb against the rake-free control: RFI change per seat UTG -1.1, UTG1 -0.4, LJ -0.2, HJ -0.1, CO -5.1, BTN -3.8, SB +10.9
