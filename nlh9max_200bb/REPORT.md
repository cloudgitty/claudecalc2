# NLH 8-handed, 200bb, high-rake live cash: approximate equilibrium strategies

**What this is.** An approximate equilibrium of an *abstracted* game, not an exact solution of
no-limit hold'em. The preflop game is solved with a payoff model for the postflop play; the
postflop game is solved heads-up on 100 flops per spot, to the end of the turn, with the river
replaced by an exact equity showdown. Every abstraction is stated with the error measured for it.
This is a separate, complete run at 200bb: the engine is the one built for the 100bb version
(`../nlh9max`), but **no 100bb result is reused** - the equity table, flop set, trees, realisation
factors and every solve were recomputed here. `PROGRESS.md` has the stage log.

## 1. Game and parameters

All parameters are in `config.json`. The seat list in the task has eight seats (UTG, UTG1, LJ,
HJ, CO, BTN, SB, BB), so the game is 8-handed. Blinds 0.5/1, no ante, no straddle, **200bb**
effective. Rake 10% of the pot, cap 5bb, no flop no drop, no jackpot drop.

Sizes exactly as given: open 3bb (SB 3.5bb, SB may complete); 3-bet 3x in position / 5x out of
position; squeeze +1x (read as a flat +1x regardless of the number of callers); 4-bet 2.5x / 2.8x,
all-in at 40% of the effective stack; 5-bet all-in. Postflop 33% / 66% / 125% pot, raises 2.5x or
all-in, anything reaching the stack is all-in, the out-of-position caller may lead the flop for 33%
only. **200bb rule:** on a non-river street, a listed bet or raise that would put in more than 60% of
the actor's remaining stack is offered together with an all-in (`allin_extra_frac = 0.60`).

**How often the 4-bet cap triggered: never.** The tree has 617 4-bet decisions; the largest 4-bet
is 50.4bb, below the 80bb (40% of 200bb) that would turn it into an all-in.

## 2. Choices made where the task is silent (defaults)

| # | Question | Choice |
| --- | --- | --- |
| 1 | 9-max but 8 seats listed | 8 players, the 8 named seats |
| 2 | BB raise size against an SB limp | 3bb; the raise is then treated as an open |
| 3 | "In position" for a 3-bet | the 3-bettor acts after the opener postflop (SB and BB never) |
| 4 | Players allowed to see a flop | at most 3, and 3-way only in single-raised pots |
| 5 | Calls of 3-bets and larger | only while the raiser is the only matched player (3-bet+ pots stay heads-up) |
| 6 | Cold 4-bets and 5-bets | not in the tree (a player without chips in voluntarily may only fold facing a 3-bet or more) |
| 7 | Preflop all-in | raked (the flop is dealt), equity showdown |
| 8 | Postflop raise count | one raise per street (then fold / call / all-in) |
| 9 | Donk lead | only the out-of-position non-raiser's first flop decision; turn leads use all sizes |
| 10 | "First bet size" for output B nodes 3 and 4 | the smallest bet (33%) |
| 11 | Rake on a pot won by a fold | from the matched pot; the uncalled bet is returned first |
| 12 | Hand classes reaching the flop under 1% of the time | dropped from postflop ranges (`trimmed_mass` per spot in the ranges' `spots.json`, at most 2.1%) |
| 13 | Spots with an empty range in equilibrium | not solved; file written with `"reached": false` |
| 14 | R loop not converged to 0.5 points | final R = mean of the last three damped rounds |
| 15 | 60% all-in rule and the turn-end model | applies on the flop and the turn (the solves have no river) |
| 16 | Postflop solves that turned numerically unstable | kept at their best iteration count (section 6) |

## 3. Preflop solve

**Tree.** `src/preflop_tree.py`, every sequence allowed by the sizes, depth limit 5-bet,
pruned by choices 4-6: 16,395 nodes (8,412 decisions, 1,656 flop terminals of which 56 are 3-way,
3,585 all-in terminals, 2,742 fold terminals).

**Hand abstraction.** 169 hand classes. Exact heads-up all-in equity for every pair of the
1,326 combos (all 1,712,304 boards; 47,008 suit/seat-symmetric classes; `src/tools/equity.c`,
22 minutes on 4 cores), reduced to class-vs-class equity and exact combo-pair counts.

**Solver.** Vector-form Discounted CFR (alpha 1.5, beta 0, gamma 2) over the public tree with a
169-vector per player, all 8 seats updated each iteration (`src/preflop.c`). Card removal between
the hero and each opponent is exact (combo-pair counts); opponents are treated as independent of
each other (card removal among opponents, including folded players, is ignored - a standard
approximation, not measured here).

**Exploitability.** Best response of each seat against the average strategies of the others,
with the same payoff model, in bb/100 (final run, 5,000 iterations): UTG 0.013, UTG1 0.021,
LJ 0.024, HJ 0.032, CO 0.043, BTN 0.038, SB 0.041, BB 0.040; **worst 0.043 bb/100** (target 1.0).
Every intermediate round and every rake variant also finished under 0.1 bb/100.

**Payoff model at flop terminals (section 2.4 of the task).**
- Heads-up: hero's value = (pot - expected rake) x R(h) x e(h, v) / Z - preflop investment, where
  e is the exact equity, R is the realisation factor measured from postflop solves (section 5),
  and Z normalises the two players' realised shares to add up to the pot net of rake under the
  current ranges (without it the model created money in big pots).
- 3-way: no 3-way postflop solves were affordable (task's fallback): equity with a fixed
  penalty. 3-way equity is not the product of pairwise equities; an additive model (product of
  the two pairwise equities plus an equal third of the probability the product misses) was fitted
  on 20,000 exact 3-way all-ins: RMSE 0.057 of the pot, against 0.094 for the plain product and
  0.079 for the normalised product; in the top range (true share 0.7-1.0) 0.739 predicted vs
  0.752 true. Fixed realisation penalty by postflop order 0.85 / 0.92 / 1.0, rake from the
  heads-up rake fit at the same pot size. **Weak point.**
- All-in: exact equity, rake min(10% pot, 5bb).
- Folds: no rake.

## 4. Postflop solves

`src/tools/postflop.c` on the heads-up DCFR solver from this repository's ICMKit (combo ranges,
suit isomorphism, O(n) showdowns), extended with rake on every awarded pot, the 33% donk
restriction, all-in only as a raise, the 200bb 60%-of-stack all-in option, and **an equity terminal at the end of the turn**: when the
turn betting closes, the pot goes to the hands in proportion to their exact equity over the 44
rivers (the task's stated reduction "go to a turn-end equity model", used for every spot). The
river equity per (turn card, hero combo, villain combo) is precomputed and stored exactly as a
count of half-wins out of 88 in one byte. Checked against the solver's exact river sweep (in the 100bb
build, same code): EV within 0.013bb on every combo.

Stop rule 0.5% of the pot (both players' best-response gain), cap 1,000 iterations.

**Flops.** All 22,100 flops reduce to 1,755 strategically different ones; these are clustered
inside texture strata (suit pattern x paired) on high/middle/low rank, connectivity, straight
possibility, broadway count and ace/king high, with clusters per stratum proportional to its share
of all flops (k-medoids, fixed seed). The 100 medoids and their weights are in `flops.json`.
**All 100 flops are solved for every reachable spot.** The 200bb task allows cutting to 30 flops;
the measured cost (1.9 core-hours per 12-flop calibration round, ~16 core-hours per full pass) fit
the budget, and the user had asked on the 100bb build not to cut flops, so no flop was cut.

## 5. Realisation factor R and expected rake

Measured from the postflop solves exactly as in the 100bb build: per hand class the realised
share of the flop pot (rake included), rho = the expected rake fraction, and
R(h) = V(h) / ((1 - rho) P e(h)); 5 strength buckets per spot and side; a per-bucket regression on
(in position, preflop aggressor, log SPR) for unsolved position pairs. R by pot type (mean of the
fits of rounds n3-n5, which the final solve averages; "aggressor" = the preflop raiser):

| Pot type | Side | R mean | R by strength bucket (weakest -> strongest) | expected rake, % of flop pot | bucket-model RMSE of the realised share | samples |
| --- | --- | --- | --- | --- | --- | --- |
| srp | IP aggressor | 1.12 | 0.91 / 1.02 / 1.09 / 1.15 / 1.49 | 19.4 | 0.101 | 12 |
| srp | IP caller | 0.49 | 0.56 / 0.52 / 0.48 / - / 1.00 | 15.0 | 0.021 | 4 |
| srp | OOP aggressor | 1.64 | 1.81 / 1.68 / 1.57 / 1.55 / 1.59 | 15.0 | 0.115 | 4 |
| srp | OOP caller | 0.82 | 0.74 / 0.81 / 0.84 / 0.89 / 0.91 | 19.4 | 0.092 | 12 |
| 3bp | IP aggressor | 1.34 | 1.42 / 1.35 / 1.27 / 1.16 / 1.52 | 12.7 | 0.122 | 9 |
| 3bp | IP caller | 0.75 | 0.89 / 0.69 / 0.74 / 0.83 / 0.83 | 11.2 | 0.067 | 6 |
| 3bp | OOP aggressor | 1.16 | 1.04 / 1.01 / 1.07 / 1.15 / 1.62 | 11.2 | 0.081 | 6 |
| 3bp | OOP caller | 0.66 | 0.61 / 0.76 / 0.76 / 0.86 / 0.79 | 12.7 | 0.045 | 9 |
| 4bp | IP aggressor | 1.19 | 1.19 / 1.23 / 1.11 / 1.13 / 1.30 | 5.8 | 0.103 | 3 |
| 4bp | IP caller | 0.90 | 0.93 / 0.69 / 0.71 / 1.10 / 1.23 | 9.8 | 0.082 | 3 |
| 4bp | OOP aggressor | 1.02 | 0.92 / 1.10 / 0.78 / 0.98 / 1.40 | 9.8 | 0.117 | 3 |
| 4bp | OOP caller | 0.71 | 0.62 / 0.84 / 1.01 / 0.63 / 0.62 | 5.8 | 0.044 | 3 |

The 200bb effect the task describes shows in the rake column: single-raised pots still lose
15-19% of the starting pot to rake, 3-bet pots 11-13%, 4-bet pots 6-10% (5bb cap on a 52-76bb pot),
and stacked-off pots 5bb of up to 400bb.

**Iteration (section 2.5).** Ranges -> calibration solves (12 flops per spot) -> R fit -> preflop
solve, damped 50/50 from round n2. **The 0.5-point criterion was not met.** RFI widths
UTG / UTG1 / LJ / HJ / CO / BTN / SB:

| Round | Widths |
| --- | --- |
| stage 3 (fixed R) | 9.9 / 11.7 / 14.3 / 18.0 / 23.8 / 34.3 / 32.3 |
| n1 (R fitted on stage 3) | 11.1 / 12.5 / 14.1 / 20.4 / 26.9 / 36.2 / 47.8 |
| n2 | 11.0 / 11.5 / 13.7 / 17.1 / 25.7 / 37.4 / 46.2 |
| n3 | 10.9 / 11.8 / 13.6 / 17.6 / 25.2 / 35.5 / 49.0 |
| n4 | 11.7 / 12.7 / 17.0 / 20.9 / 23.8 / 35.2 / 52.9 |
| n5 | 11.8 / 12.9 / 16.6 / 20.0 / 24.4 / 35.0 / 51.4 |
| **final (mean R of n3-n5)** | **11.4 / 12.7 / 14.8 / 19.3 / 24.5 / 35.3 / 51.5** (SB limp 3.8) |

**Measured uncertainty of the final widths from the R loop: about +/-1 point for UTG/UTG1,
+/-2 for LJ-BTN, +/-3 for SB** (round n4 moved LJ, HJ and SB by 3-4 points).

## 6. Spots

Final solve (stage 7): 12 reachable spots x 100 flops = 1,200 solves, ranges from the final
preflop solution. Exploitability in % of the pot, the larger of the two players'.

| Spot | OOP / IP | Pot | Stack | SPR | Range combos OOP / IP | Donk limited | Flops | Exploitability median / worst | Median s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| srp_BTN_vs_BB | BB / BTN | 6.5 | 197 | 30.31 | 42.1 / 467.7 | yes | 100 | 0.486 / 0.499 | 32 |
| srp_BTN_vs_SB | SB / BTN | 7 | 197 | 28.14 | 0.0 / 467.7 | yes | not reached | - | - |
| srp_CO_vs_BTN | CO / BTN | 7.5 | 197 | 26.27 | 323.8 / 0.0 | no | not reached | - | - |
| srp_CO_vs_BB | BB / CO | 6.5 | 197 | 30.31 | 78.1 / 323.8 | yes | 100 | 0.488 / 0.500 | 37 |
| srp_HJ_vs_BB | BB / HJ | 6.5 | 197 | 30.31 | 86.7 / 255.6 | yes | 100 | 0.487 / 0.500 | 37 |
| srp_UTG_vs_BB | BB / UTG | 6.5 | 197 | 30.31 | 104.4 / 150.9 | yes | 100 | 0.487 / 0.500 | 26 |
| srp_SB_vs_BB | SB / BB | 7 | 196.5 | 28.07 | 682.1 / 4.0 | no | 100 | 0.487 / 0.802 | 47 |
| srp_UTG_vs_BTN | UTG / BTN | 7.5 | 197 | 26.27 | 150.9 / 0.0 | no | not reached | - | - |
| 3bp_BTN_vs_BB | BB / BTN | 30.5 | 185 | 6.07 | 246.6 / 6.4 | no | 100 | 0.464 / 0.500 | 23 |
| 3bp_CO_vs_BTN | CO / BTN | 19.5 | 191 | 9.79 | 4.0 / 201.7 | yes | 100 | 0.477 / 0.499 | 5 |
| 3bp_HJ_vs_CO | HJ / CO | 19.5 | 191 | 9.79 | 24.5 / 129.6 | yes | 100 | 0.477 / 0.500 | 8 |
| 3bp_UTG_vs_BB | BB / UTG | 30.5 | 185 | 6.07 | 45.3 / 11.6 | no | 100 | 0.471 / 0.500 | 8 |
| 3bp_SB_vs_BB | SB / BB | 21 | 189.5 | 9.02 | 7.4 / 436.5 | yes | 100 | 0.465 / 0.498 | 9 |
| 4bp_BTN_vs_BB | BB / BTN | 75.5 | 162.5 | 2.15 | 36.6 / 112.7 | yes | 100 | 0.447 / 0.499 | 2 |
| 4bp_CO_vs_BTN | CO / BTN | 51.9 | 174.8 | 3.37 | 108.0 / 65.1 | no | 100 | 0.464 / 0.499 | 5 |

**Worst postflop exploitability: 0.802% of the pot** (one solve; every other solve <= 0.500%).
`srp_SB_vs_BB` on Qc8d6h: the BB's range in this spot is a single hand class (A8s, 3 live combos on
that board). The solve reached 0.80% at 300 iterations and then diverged (373.7% at the 1,000
cap); it was re-solved to its best point, 300 iterations. A second narrow-range solve (in a rake
variant's calibration: `3bp_UTG_vs_BB` on Jc8d3h, AK only) returned NaN and was left out of that
fit. Both are numerical instabilities of the solver with ranges of a few combos at 200bb; neither
affects a spot with a normal range.

**Not reached (3 of the 15 spots):** the in-position player never flat-calls: the BTN 3-bets or
folds against a CO, UTG or SB open. Their files exist with `"reached": false` and no flops.
Several reached spots have very small ranges for one side (BB calls an SB open with 4 combos, CO
calls a BTN 3-bet with 4, BTN calls a BB 3-bet with 6): equilibrium play for those narrow ranges.

## 7. Weak points (largest first) and the errors measured

1. **The R model.** One factor per (spot, side, strength bucket) multiplying equity, fitted on 12
   flops per spot; bucket-model RMSE of the realised share 0.02-0.12 of the pot (section 5).
   R depends on equity only, not on the kind of hand: playability (suited connectors, small pairs)
   is not represented. The preflop <-> R loop did not reach the 0.5-point criterion.
2. **The independence approximation is not money-conserving** (measured in the 100bb build with the
   same code: +0.066 bb per hand summed over the 8 seats with rake 0 and R = 1). The exploitability
   figures are measured inside the model and do not include this bias.
3. **3-way pots.** Equity with a fixed penalty (additive 3-way equity model, RMSE 0.057 of the pot
   against exact all-ins), no 3-way solves.
4. **No river.** Every postflop solve ends at the turn with an exact equity showdown. At 200bb this
   costs more than at 100bb: three streets of 125% bets are possible in the full game, two here,
   so river bluffing and river stack-offs are missing; the rake is taken at the turn-end pot.
5. **Sparse postflop coverage.** 12 reachable heads-up spots, 100 flops each; only the four root
   decisions are stored.
6. **Pruned preflop tree** (choices 4-6).
7. **Numerical instability with very narrow ranges** (section 6): 2 of 3,185 solves.
8. **Rake variants** (control and sensitivity) use three R rounds each from the final ranges; their
   differences from the main run are of the same size as the R-loop noise.

## Validation summary (section 5; full output in `validation.md`)

- Probability sums: raw strategies sum to 1 within 1.2e-7; quantised percents sum to 100 and are
  within 3.3 points of the raw frequency. PASS.
- Round trip with the independent decoder (`src/decoder_ref.py`): 100,000 preflop and 100,008
  postflop entries, 0 differences. PASS.
- RFI width UTG 11.4 < UTG1 12.7 < LJ 14.8 < HJ 19.3 < CO 24.5 < BTN 35.3. PASS.
- BB defence widest against SB (33.3%) and BTN (21.9%); CO 15.7, UTG 11.3. PASS.
- 4-bet and 5-bet narrower than 3-bet (% of all combos): BB 3-bet vs BTN 18.7, BTN 4-bet 8.5,
  BB 5-bet 5.4. PASS.
- **In position, 3-bet narrower than call and polarised: FAIL.** The in-position player never flats
  an open (BTN vs CO 3-bet 15.3 / call 0.0; CO vs HJ 9.8 / 0.0; BTN vs UTG 6.1 / 0.0; HJ vs UTG
  5.8 / 0.0): linear 3-bet-or-fold, so there is no call range to compare with. A result of the
  solve under this rake, as at 100bb.
- **Rake tightens every non-blind RFI range vs the rake-free control: FAIL.** UTG -0.7 and UTG1
  -0.2 are tighter with rake; LJ +0.1, HJ +1.3, CO +1.9, BTN +1.3 are wider. At 200bb the rake is
  a small share of the big pots the late positions play for (5bb of a stacked 400bb pot), and the
  differences are within the R-loop noise (+/-2 points); the clear rake effects are elsewhere: the
  SB limps 14.1% without rake and 3.8% with it, and the BB calls a BTN open 13.5% without rake and
  3.2% with it (`rake_report.md`).
- Hand sanity table for AA, KK, AKs, AKo, 22, 87s, A5s, K9o, 72o in the 8 RFI spots and BB vs BTN:
  `validation.md` section 4.
- Exploitability: preflop worst 0.043 bb/100 (PASS); postflop worst 0.802% of the pot (**FAIL**,
  one degenerate solve, section 6).
- Sensitivity (rake cap 3bb and rake 5%; BTN, CO and BB spots): `rake_report.md`. A lower cap or
  lower rate moves BTN/CO opening by under 2 points either way (within noise); the BB's response to
  a BTN open shifts from call 3.2 / 3-bet 18.7 to call 1.7 / 3-bet 19.5 (cap 3bb) and call 11.6 /
  3-bet 16.6 (rake 5%). The c-bet rows in `rake_report.md` each come from one flop of one
  calibration solve on that variant's own ranges and are noisy (they swing 30-70 points between
  variants); treat them as illustrations only.
- Published GTO charts were not used: no numbers are quoted from memory.

## 8. Licences

All code in `src/` is original to this repository (the postflop solver core is the ICMKit solver
written earlier in this repository). No third-party solver, evaluator or library code is included;
dependencies are the C standard library, POSIX threads, OpenMP (gcc), Python 3 and numpy (BSD).

## 9. Results, sizes and commands

**Outputs** (total 1.37 MB, under the 15 MB target):

| File | Size |
| --- | --- |
| `preflop.json` (149 records, reach > 0.1%) | 56,893 bytes |
| `preflop_ev.bin` (149 x 169 float16) | 50,362 bytes |
| `postflop/*.bin.gz` (15 files, 12 with 100 flops) | 1,191,744 bytes |
| `test_vectors.json` (300 + 300) | 119,712 bytes |
| `rake_report.md`, `validation.md`, `flops.json`, `combos.json` | 2 KB, 4 KB, 35 KB, 18 KB |

**Compute.** Wall clock 00:46 - 13:15 UTC on 2026-11-04, about 12.5 hours (including a container
restart at 01:36 that the pipeline resumed from). CPU: postflop solves 21.7 core-hours (3,185 solves:
calibration rounds, the final 1,200-flop pass, three rake variants with three rounds each),
preflop solves about 25 core-hours (16 runs, ~68,000 iterations at ~1.3 core-seconds each),
exact equity table 1.4 core-hours; **about 48 core-hours in total**.

**Output formats.** Identical to the 100bb build (same `combos.json` enumeration):
- `preflop.json`: `{"records": [{"id", "path", "hero", "actions", "reach", "node", "hands"}]}`.
  `id` is `<hero>:open` or `<hero>:vs_<voluntary actions>`; `actions` uses `f`, `c` (call or
  check), `r<to>` (raise to that many bb), `a` (all-in); `hands` is `AA=r100 AKs=r85,c15 ...`
  with `r` for the raise, percents in 5% steps, folds implied, 0% omitted.
- `preflop_ev.bin`: float16 little-endian, 169 values per record in record order; hand classes in
  the order of `hand_classes` in `combos.json` (13 x 13 grid, A..2, suited above the diagonal);
  EV in bb of the acting hand from the start of the hand, under the final strategy.
- `postflop/<pottype>_<hero>_vs_<villain>.bin.gz` (hero = preflop raiser of the spot): gzip
  (deflate level 9) of `"NLHPOST1"`, uint32 header length, the JSON header, then the body. For
  each flop in `header.flops` at its `offset`: uint8 reach[OOP][1326] and reach[IP][1326] (0-255,
  scaled by `reach_scale`), then for each of the four root nodes with actions
  (`oop_root`, `ip_vs_check`, `ip_vs_bet` facing OOP's smallest bet, `oop_vs_checkbet` facing IP's
  smallest bet after a check) uint8[1326][nact]: indices into `freq_table` (0, 5, ..., 100),
  summing to 20 for in-range combos, all 0 for combos not in the range. Action names are in the
  header (`check`, `bet33`, `bet66`, `bet125`, `fold`, `call`, `raise<to>`, `allin`).
- Combo order: `combos.json` (fixed; will not change).

**Commands** (from `nlh9max_200bb/`):

```
make && make test
./build/equity work                                   # stage 1
python3 src/preflop_tree.py config.json work/tree.json
python3 src/flops.py 100 flops.json
sh src/pipeline.sh                                    # stages 3-8, resumable
sh src/variant_iter.sh work/var_rake0 work/var_rake0b 0 0     # extra variant rounds (b, c),
                                                      # likewise cap3 (0.10 3) and pct5 (0.05 5)
python3 src/validate.py . work/final/pre work/final/ranges work/final/pre_solves work/var_rake0c/pre
python3 src/rake_report.py rake_report.md work/final/pre work/var_rake0c/pre "cap 3bb=work/var_cap3c/pre" "rake 5%=work/var_pct5c/pre"
```
