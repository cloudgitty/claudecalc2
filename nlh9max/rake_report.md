# Effect of the rake (output C)

Widths are % of all 1,326 combos. Every column is a full re-solve of the same tree,
same sizes and same iteration count; only the rake parameters differ.

| Measure | rake 10% / 5bb | rake 0 (control) | cap 3bb | rake 5% |
| --- | --- | --- | --- | --- |
| RFI UTG | 9.1 | 9.5 | 8.9 | 9.5 |
| RFI UTG1 | 11.8 | 11.1 | 11.5 | 12.1 |
| RFI LJ | 14.2 | 13.2 | 13.6 | 14.4 |
| RFI HJ | 18.2 | 17.2 | 17.5 | 17.3 |
| RFI CO | 26.3 | 26.6 | 25.0 | 25.9 |
| RFI BTN | 45.7 | 53.9 | 46.3 | 48.7 |
| RFI SB | 48.9 | 42.2 | 49.0 | 50.2 |
| SB limp | 0.0 | 0.1 | 0.0 | 0.0 |
| BB vs BTN open: call | 3.2 | 5.8 | 2.1 | 4.1 |
| BB vs BTN open: 3-bet | 18.8 | 17.2 | 19.0 | 17.6 |
| BB vs SB open: call | 4.9 | 30.3 | 4.2 | 9.3 |
| BB vs SB open: 3-bet | 30.3 | 21.3 | 30.9 | 28.2 |
| BB vs CO open: call | 0.0 | 0.0 | 0.0 | 0.0 |
| BB vs CO open: 3-bet | 11.1 | 10.4 | 10.8 | 11.1 |
| BB vs UTG open: call | 0.5 | 0.3 | 0.0 | 0.1 |
| BB vs UTG open: 3-bet | 4.1 | 3.9 | 3.9 | 4.4 |
| BTN vs BB 3-bet: call | 0.0 | 15.0 | 0.1 | 7.1 |
| BTN vs BB 3-bet: 4-bet | 24.3 | 16.9 | 24.3 | 20.3 |

## Overall

- rake 10% / 5bb: sum of the 7 RFI widths 174.2 points (UTG 9.1 ... BTN 45.7), SB limp 0.0
- rake 0 (control): sum of the 7 RFI widths 173.8 points (UTG 9.5 ... BTN 53.9), SB limp 0.1
- cap 3bb: sum of the 7 RFI widths 171.8 points (UTG 8.9 ... BTN 46.3), SB limp 0.0
- rake 5%: sum of the 7 RFI widths 178.2 points (UTG 9.5 ... BTN 48.7), SB limp 0.0
- rake 10%/5bb against the rake-free control: RFI change per seat UTG -0.4, UTG1 +0.7, LJ +0.9, HJ +1.0, CO -0.3, BTN -8.2, SB +6.8
