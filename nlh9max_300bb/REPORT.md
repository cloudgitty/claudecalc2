# NLH 8-handed, 300bb, high-rake live cash: approximate equilibrium strategies

**What this is.** An approximate equilibrium of an *abstracted* game, not an exact solution of
no-limit hold'em. The preflop game is solved with a payoff model for the postflop play; the
postflop game is solved heads-up on 100 flops per spot, to the end of the turn, with the river
replaced by an exact equity showdown. Every abstraction is stated with the error measured for it.
This is a separate run at 300bb. Reused from the earlier builds, as the task allows: the code, the
exact hand equity tables and the flop clustering. **Not reused:** trees, realisation factors, any
solve or any result at another depth. `PROGRESS.md` has the stage log.

## 1. Game and parameters

All parameters are in `config.json`. The seat list in the task has eight seats (UTG, UTG1, LJ,
HJ, CO, BTN, SB, BB), so the game is 8-handed. Blinds 0.5/1, no ante, no straddle, **300bb**
effective. Rake 10% of the pot, cap 5bb, no flop no drop, no jackpot drop.

Sizes exactly as given: open 3bb (SB 3.5bb, SB may complete); 3-bet 3x in position / 5x out of
position; squeeze +1x (read as a flat +1x regardless of the number of callers); 4-bet 2.5x / 2.8x,
all-in at 40% of the effective stack; 5-bet all-in. Postflop 33% / 66% / 125% pot, raises 2.5x or
all-in, anything reaching the stack is all-in, the out-of-position caller may lead the flop for 33%
only. **300bb rule:** on a non-river street, a listed bet or raise that would put in more than 50% of
the actor's remaining stack is offered together with an all-in (`allin_extra_frac = 0.50`).

**How often the 4-bet cap triggered: never.** 617 4-bet decisions; the largest 4-bet is 50.4bb, far
below the 120bb (40% of 300bb) that would turn it into an all-in.

**The 5-bet jam.** After a 4-bet the 3-bettor may fold, call or jam 300bb, and facing the jam the
4-bettor folds or calls, with pot odds from the actual investments. Result for the BB facing the
BTN's 4-bet to 37.5bb: fold 49.5%, call 28.2% (a flop with SPR 3.5), jam 22.3% of its 3-bet range.
The jams are AA, KK, QQ, AKs, AKo, KQs, 55 and the blocker bluffs A5s and A4s - only the strongest
hands and a few blockers, and many 4-bet pots go to a flop, as the task expected.

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
| 12 | Hand classes reaching the flop under 1% of the time | dropped from postflop ranges (`trimmed_mass` per spot in the ranges' `spots.json`) |
| 13 | Spots with an empty range in equilibrium | not solved; file written with `"reached": false` |
| 14 | R loop not converged to 0.5 points | final R = mean of the last three damped rounds |
| 15 | 50% all-in rule and the turn-end model | applies on the flop and the turn (the solves have no river) |
| 16 | R for speculative hands | per hand class for solved spots, shrunk toward the strength-bucket value with a 6-combo prior; regression by bucket for unsolved position pairs |

## 3. Preflop solve

**Tree.** `src/preflop_tree.py`, every sequence allowed by the sizes, depth limit 5-bet,
pruned by choices 4-6: 16,395 nodes (8,412 decisions, 1,656 flop terminals of which 56 are 3-way,
3,585 all-in terminals, 2,742 fold terminals).

**Hand abstraction.** 169 hand classes. Exact heads-up all-in equity for every pair of the
1,326 combos (all 1,712,304 boards; 47,008 suit/seat-symmetric classes; `src/tools/equity.c`,
computed for the 200bb build and reused here, as the task
allows), reduced to class-vs-class equity and exact combo-pair counts.

**Solver.** Vector-form Discounted CFR (alpha 1.5, beta 0, gamma 2) over the public tree with a
169-vector per player, all 8 seats updated each iteration (`src/preflop.c`). Card removal between
the hero and each opponent is exact (combo-pair counts); opponents are treated as independent of
each other (card removal among opponents, including folded players, is ignored - a standard
approximation, not measured here).

**Exploitability.** Best response of each seat against the average strategies of the others,
with the same payoff model, in bb/100 (final run, 5,000 iterations): UTG 0.012, UTG1 0.020,
LJ 0.017, HJ 0.017, CO 0.014, BTN 0.021, SB 0.016, BB 0.025; **worst 0.025 bb/100** (target 1.0).
Every intermediate round and every rake variant also finished under 0.2 bb/100 (worst 0.188).

**Payoff model at flop terminals (section 2.4 of the task).**
- Heads-up: hero's value = (pot - expected rake) x R(h) x e(h, v) / Z - preflop investment, where
  e is the exact equity, R is the realisation factor measured from postflop solves (section 5,
  per hand class for the solved spots),
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
restriction, all-in only as a raise, the 300bb 50%-of-stack all-in option, and **an equity terminal at the end of the turn**: when the
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
The flop set is the one clustered for the 100bb build (the task allows reusing the clustering).
**All 100 flops are solved for every reachable spot.** The 300bb task allows cutting to 24 flops;
the measured cost (2.9 core-hours per 12-flop calibration round, ~24 core-hours per full pass) fit
the budget, and the user had asked on the 100bb build not to cut flops, so no flop was cut.

**Divergence guard.** A solve that ends above the 0.5% target and above 1.5x the best point it
reached is rebuilt and re-solved to that best iteration count (DCFR is deterministic); non-finite
results are written as 999999. At 300bb it never fired: all 3,434 solves converged normally.

## 5. Realisation factor R and expected rake

Measured from the 300bb postflop solves: per hand class the realised share of the flop pot (rake
included), rho = the expected rake fraction, and R(h) = V(h) / ((1 - rho) P e(h)). **New at 300bb:**
the solved spots use R per hand class (169 values), so suited connectors, small pairs and suited
aces keep their own measured realisation instead of a strength-bucket average; a class with few
combos in range is shrunk toward its bucket (6-combo prior). This halves the model error (last
column). Unsolved position pairs use a per-bucket regression on (in position, preflop aggressor,
log SPR). Example, BB defending against a BTN open (round n5): 76s 0.89, 87s 0.88, QJo 0.86, AJo
0.83, KQo 0.76, 22 0.74, K9o 0.72, A5s 0.72 (bucket values 0.72-0.89) - the speculative suited hands
realise more of their equity than offsuit hands of similar strength.

R by pot type (mean of the fits of rounds n3-n5, which the final solve averages; "aggressor" = the
preflop raiser):

| Pot type | Side | R mean | R by strength bucket (weakest -> strongest) | expected rake, % of flop pot | RMSE of realised share: bucket / per-class model | samples |
| --- | --- | --- | --- | --- | --- | --- |
| srp | IP aggressor | 1.13 | 1.08 / 1.06 / 1.02 / 1.12 / 1.37 | 18.4 | 0.100 / 0.053 | 14 |
| srp | IP caller | 1.02 | 1.22 / 1.03 / 0.86 / 0.97 / 1.00 | 20.3 | 0.075 / 0.037 | 3 |
| srp | OOP aggressor | 0.97 | 1.05 / 0.79 / 0.80 / 0.99 / 1.22 | 20.3 | 0.108 / 0.054 | 3 |
| srp | OOP caller | 0.84 | 0.93 / 0.76 / 0.85 / 0.87 / 0.92 | 18.4 | 0.072 / 0.043 | 14 |
| 3bp | IP aggressor | 1.11 | 1.05 / 0.99 / 1.07 / 1.08 / 1.40 | 13.8 | 0.124 / 0.068 | 9 |
| 3bp | IP caller | 0.87 | 1.03 / 0.84 / 0.73 / 0.90 / 1.09 | 12.3 | 0.048 / 0.037 | 6 |
| 3bp | OOP aggressor | 1.00 | 0.83 / 0.88 / 0.93 / 0.91 / 1.66 | 12.3 | 0.099 / 0.056 | 6 |
| 3bp | OOP caller | 0.80 | 0.76 / 0.71 / 1.02 / 0.77 / 1.08 | 13.8 | 0.046 / 0.037 | 9 |
| 4bp | IP aggressor | 1.08 | 1.06 / 1.15 / 0.89 / 1.04 / 1.28 | 5.6 | 0.131 / 0.074 | 3 |
| 4bp | IP caller | 1.01 | 1.11 / 0.97 / 1.03 / 0.83 / 1.14 | 9.6 | 0.090 / 0.050 | 3 |
| 4bp | OOP aggressor | 0.96 | 0.95 / 0.80 / 0.80 / 1.03 / 1.25 | 9.6 | 0.089 / 0.052 | 3 |
| 4bp | OOP caller | 0.83 | 0.68 / 0.96 / 0.85 / 0.77 / 1.03 | 5.6 | 0.039 / 0.025 | 3 |

Rake: single-raised pots still lose 18-20% of the starting pot to rake; 3-bet pots 12-14%; 4-bet
pots 6-10% (the 5bb cap on a 52-76bb pot); stacked-off pots 5bb of up to 600bb.

**Iteration (section 2.5).** Ranges -> calibration solves (12 flops per spot) -> R fit -> preflop
solve, damped 50/50 from round n2. **The 0.5-point criterion was not met.** RFI widths
UTG / UTG1 / LJ / HJ / CO / BTN / SB:

| Round | Widths |
| --- | --- |
| stage 3 (fixed R) | 10.6 / 12.1 / 14.5 / 17.9 / 24.0 / 34.7 / 33.1 |
| n1 (R fitted on stage 3) | 14.0 / 15.5 / 17.5 / 23.1 / 27.3 / 40.8 / 55.2 |
| n2 | 10.9 / 14.0 / 17.0 / 17.8 / 20.9 / 36.8 / 45.1 |
| n3 | 9.5 / 11.8 / 15.3 / 19.1 / 20.4 / 34.2 / 39.3 |
| n4 | 10.1 / 12.2 / 15.4 / 18.4 / 20.1 / 34.6 / 41.8 |
| n5 | 10.1 / 11.6 / 13.8 / 18.1 / 21.9 / 41.0 / 34.1 |
| **final (mean R of n3-n5)** | **10.0 / 11.8 / 14.8 / 18.4 / 20.8 / 38.2 / 38.7** (SB limp 15.8) |

n3 -> n4 moved UTG-BTN by at most 0.7 points; n5 then moved BTN +6.4 and SB -7.7. **Measured
uncertainty of the final widths from the R loop: about +/-1 point for UTG-CO, +/-4 for BTN, +/-5
for SB.**

## 6. Spots

Final solve (stage 7): 12 reachable spots x 100 flops = 1,200 solves, ranges from the final
preflop solution. Exploitability in % of the pot, the larger of the two players'.

| Spot | OOP / IP | Pot | Stack | SPR | Range combos OOP / IP | Donk limited | Flops | Exploitability median / worst | Median s | Max iterations |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| srp_BTN_vs_BB | BB / BTN | 6.5 | 297 | 45.69 | 194.5 / 506.6 | yes | 100 | 0.490 / 0.500 | 161 | 640 |
| srp_BTN_vs_SB | SB / BTN | 7 | 297 | 42.43 | 0.0 / 506.6 | yes | not reached | - | - | - |
| srp_CO_vs_BTN | CO / BTN | 7.5 | 297 | 39.60 | 276.3 / 0.0 | no | not reached | - | - | - |
| srp_CO_vs_BB | BB / CO | 6.5 | 297 | 45.69 | 165.4 / 276.3 | yes | 100 | 0.490 / 0.500 | 96 | 790 |
| srp_HJ_vs_BB | BB / HJ | 6.5 | 297 | 45.69 | 121.8 / 244.4 | yes | 100 | 0.491 / 0.500 | 82 | 810 |
| srp_UTG_vs_BB | BB / UTG | 6.5 | 297 | 45.69 | 75.3 / 131.6 | yes | 100 | 0.490 / 0.500 | 42 | 790 |
| srp_SB_vs_BB | SB / BB | 7 | 296.5 | 42.36 | 512.7 / 175.3 | no | 100 | 0.489 / 0.500 | 298 | 660 |
| srp_UTG_vs_BTN | UTG / BTN | 7.5 | 297 | 39.60 | 131.6 / 0.0 | no | not reached | - | - | - |
| 3bp_BTN_vs_BB | BB / BTN | 30.5 | 285 | 9.34 | 205.5 / 4.3 | no | 100 | 0.470 / 0.499 | 45 | 320 |
| 3bp_CO_vs_BTN | CO / BTN | 19.5 | 291 | 14.92 | 36.7 / 234.9 | yes | 100 | 0.482 / 0.499 | 25 | 440 |
| 3bp_HJ_vs_CO | HJ / CO | 19.5 | 291 | 14.92 | 31.3 / 142.6 | yes | 100 | 0.477 / 0.500 | 11 | 330 |
| 3bp_UTG_vs_BB | BB / UTG | 30.5 | 285 | 9.34 | 51.5 / 17.7 | no | 100 | 0.472 / 0.499 | 13 | 300 |
| 3bp_SB_vs_BB | SB / BB | 21 | 289.5 | 13.79 | 4.0 / 341.5 | yes | 100 | 0.473 / 0.500 | 8 | 340 |
| 4bp_BTN_vs_BB | BB / BTN | 75.5 | 262.5 | 3.48 | 58.2 / 121.8 | yes | 100 | 0.449 / 0.498 | 6 | 170 |
| 4bp_CO_vs_BTN | CO / BTN | 51.9 | 274.8 | 5.29 | 76.0 / 126.2 | no | 100 | 0.465 / 0.499 | 20 | 250 |

**Worst postflop exploitability: 0.500% of the pot** - every solve met the target. Single-raised
solves at SPR 40-46 need up to 1,000 iterations (2.5x the 100bb count).

**Not reached (3 of the 15 spots):** the in-position player never flat-calls the open: the BTN
3-bets or folds against a CO, UTG or SB open. Their files exist with `"reached": false` and no flops.
Several reached spots have small ranges for one side (BTN calls a BB 3-bet with 4 combos, SB calls
a BB 3-bet with 4): equilibrium play for those narrow ranges.

**What 300bb changes (all measured here):** the BB flats far more (against a BTN open: call 14.7 /
3-bet 15.6 at 300bb), the SB limps 15.8%, 4-bet pots go to the flop 28% of the time instead of being
jammed, and the in-position player still never flats an open.

## 7. Weak points (largest first) and the errors measured

1. **The R model.** Per-class R for the 12 solved spots (RMSE of the realised share 0.025-0.074 of
   the pot), bucket regression for the other position pairs (0.04-0.13); fitted on 12 flops per spot.
   The preflop <-> R loop did not reach the 0.5-point criterion (section 5).
2. **The independence approximation is not money-conserving** (measured in the 100bb build with the
   same code: +0.066 bb per hand summed over the 8 seats with rake 0 and R = 1). The exploitability
   figures are measured inside the model and do not include this bias.
3. **No river.** Every postflop solve ends at the turn with an exact equity showdown. At 300bb this
   matters most: with three streets of 125% bets possible in the full game, the deep single-raised
   pots lose river bluffing, river stack-offs and implied odds on the river; the rake is taken at
   the turn-end pot. Speculative hands' implied odds are therefore understated.
4. **3-way pots.** Equity with a fixed penalty (additive 3-way equity model, RMSE 0.057 of the pot
   against exact all-ins), no 3-way solves.
5. **Sparse postflop coverage.** 12 reachable heads-up spots, 100 flops each; only the four root
   decisions are stored.
6. **Pruned preflop tree** (choices 4-6).
7. **Rake variants** (control and sensitivity) use three R rounds each from the final ranges; the
   rake-free control's rounds still moved the BTN by 7-14 points, so its comparison with the main
   run carries that uncertainty.

## Validation summary (section 5; full output in `validation.md`)

- Probability sums: raw strategies sum to 1 within 1.2e-7; quantised percents sum to 100 and are
  within 3.3 points of the raw frequency. PASS.
- Round trip with the independent decoder (`src/decoder_ref.py`): 100,000 preflop and 100,008
  postflop entries, 0 differences. PASS.
- RFI width UTG 10.0 < UTG1 11.8 < LJ 14.8 < HJ 18.4 < CO 20.8 < BTN 38.2. PASS.
- BB defence widest against SB (39.0%) and BTN (30.3%); CO 19.0, UTG 9.6. PASS.
- 4-bet and 5-bet narrower than 3-bet (% of all combos): BB 3-bet vs BTN 15.6, BTN 4-bet 9.2,
  BB 5-bet 3.5. PASS.
- **In position, 3-bet narrower than call and polarised: FAIL.** The in-position player never flats
  an open (BTN vs CO 3-bet 17.7 / call 0.0; CO vs HJ 10.8 / 0.0; BTN vs UTG 6.5 / 0.0; HJ vs UTG
  5.9 / 0.0): linear 3-bet-or-fold, so there is no call range to compare with. Out of position the
  BB now flats a lot (vs BTN: 3-bet 15.6, call 14.7).
- **Rake tightens every non-blind RFI range vs the rake-free control: FAIL.** With rake UTG -0.5 and
  CO -1.7 are tighter; UTG1 +0.2, LJ +0.8, HJ +2.1 and **BTN +10.0** are wider. The BTN gap is real
  in this model, not noise: without rake the BB defends 71% against a BTN open (call 60.4, 3-bet
  10.6) against 30% with rake, and a BB that defends that much makes BTN steals less profitable.
  Rake's direct effect (less flatting, more 3-bet-or-fold) is clear in the blinds: the BB calls a
  BTN open 14.7% with rake, 60.4% without. The control's own rounds swung the BTN by 7-14 points
  (21.0 -> 35.2 -> 28.2), so the size of the gap is uncertain; its direction is consistent.
- Hand sanity table for AA, KK, AKs, AKo, 22, 87s, A5s, K9o, 72o in the 8 RFI spots and BB vs BTN:
  `validation.md` section 4.
- Exploitability: preflop worst 0.025 bb/100; postflop worst 0.500% of the pot. PASS.
- Sensitivity (rake cap 3bb and rake 5%; BTN, CO and BB spots): `rake_report.md`. Lowering the rake
  makes the BB flat more against a BTN open (call 14.7 -> 20.5 with cap 3bb, 28.1 with 5%) and
  3-bet less (15.6 -> 13.9 / 12.9); the BTN opens tighter (38.2 -> 32.7 / 32.4) and the CO about the
  same (20.8 -> 19.7 / 20.2) - the direction runs from the 10%/5bb solve toward the rake-free
  control. The c-bet rows in `rake_report.md` each come from one flop of one calibration solve on
  that variant's own ranges and are noisy; treat them as illustrations only.
- Published GTO charts were not used: no numbers are quoted from memory.

## 8. Licences

All code in `src/` is original to this repository (the postflop solver core is the ICMKit solver
written earlier in this repository). No third-party solver, evaluator or library code is included;
dependencies are the C standard library, POSIX threads, OpenMP (gcc), Python 3 and numpy (BSD).

## 9. Results, sizes and commands

**Outputs** (total 1.67 MB, under the 15 MB target):

| File | Size |
| --- | --- |
| `preflop.json` (142 records, reach > 0.1%) | 61,947 bytes |
| `preflop_ev.bin` (142 x 169 float16) | 47,996 bytes |
| `postflop/*.bin.gz` (15 files, 12 with 100 flops) | 1,437,466 bytes |
| `test_vectors.json` (300 + 300) | 119,750 bytes |
| `rake_report.md`, `validation.md`, `flops.json`, `combos.json` | 2 KB, 4 KB, 35 KB, 18 KB |

**Compute.** Started 2026-11-04 19:12 UTC, finished 2026-11-06 04:01 UTC (32.8 hours elapsed).
The container was down twice for long spells (19:22-22:02 and 18:15-00:52, about 9 hours) and
restarted briefly twice more; the pipeline resumed every time from the last finished step, so
**about 23.5 hours of actual running**. CPU: postflop solves 62.8 core-hours (3,434 solves:
calibration rounds, the final 1,200-flop pass, three rake variants with three rounds each),
preflop solves about 25 core-hours (16 runs, ~68,000 iterations), equity table reused (1.4
core-hours, from the 200bb build); **about 88 core-hours in total**.

**Output formats.** Identical to the 100bb and 200bb builds (same `combos.json` enumeration):
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

**Commands** (from `nlh9max_300bb/`):

```
make && make test
python3 src/preflop_tree.py config.json work/tree.json
# equity tables (work/eq1326.bin, work/eq169.bin) and flops.json reused from the 200bb build;
# to recompute: ./build/equity work; python3 src/flops.py 100 flops.json
sh src/pipeline.sh            # stages 3-8 and the three rake variants (3 rounds each), resumable
```
