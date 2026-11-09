# NLH 8-handed, 100bb, high-rake live cash: approximate equilibrium strategies

**What this is.** An approximate equilibrium of an *abstracted* game, not an exact solution of
no-limit hold'em. The preflop game is solved with a payoff model for the postflop play; the
postflop game is solved heads-up on a sample of flops, to the end of the turn, with the river
replaced by an equity showdown. Every abstraction below is stated with the error measured for
it, where it could be measured. `PROGRESS.md` has the stage log.

> **Improvement update (November 2026).** The outputs in this folder (`preflop.json`,
> `preflop_ev.bin`, `postflop/*.bin.gz`, `test_vectors.json`, `validation.md`, `rake_report.md`) were
> rebuilt with the improvement plan (`../NLH_IMPROVEMENT_PLAN.md`, items 1-8). Formats and the
> `combos.json` enumeration are unchanged. Section 0 below describes what changed and what was
> measured; sections 1-9 describe the original build and stay as its record where section 0 does not
> supersede them. The improved engine is `../nlh_improve/` (its `PROGRESS.md` has the engineering
> and gate log); this run's log is `PROGRESS2.md`, its durable checkpoints are in `checkpoints/`.

## 0. Improvement update: what changed and what was measured

| # | Change | Result at 100bb |
| --- | --- | --- |
| 1 | R refitted on all 100 flops (polish) | Two polish rounds; the second moved the RFI widths by up to 1.67 points (mostly SB) |
| 2 | Converged preflop <-> R loop, running average of R, 30 calibration flops per spot (50 for SB vs BB, BTN vs BB) | **Not met.** 10 rounds (cap); largest RFI moves 4.13, 1.62, 1.37, 1.11, 0.62, 1.10, 0.62, 0.61, 0.87, 0.40. From round 5 on the widths oscillate within about +/-0.6-0.9 points with no drift (calibration-flop noise) |
| 3 | Rake variants (rake 0, cap 3bb, rake 5%) through the same loop | **Not met** for any of the three in 10 rounds (last moves 1.10, 1.11, 1.96); the rake-free BTN was still widening by ~1 point per round at the cap |
| 4 | Hand-type features (pair, suited, connected, suited ace, broadway) in the R regression for unsolved spots | Used for every terminal without its own solves |
| 5 | Money-conserving payoffs (card removal only between players who meet; folded players as scalar masses) | Summed EV at rake 0, R = 1: **-0.023** bb per hand (was +0.066); the 0.02 tolerance is narrowly missed |
| 6 | Un-pruned preflop tree: cold 4-bets and 3-way 3-bet pots | 112,379 nodes (was 8,057); 5,000 iterations ~1.5 h on 4 cores |
| 7 | Two postflop raises per street | Everywhere (1.6-1.9x cost per solve) |
| 8 | River correction: reduced river tree (66% + all-in, no river raises) on 4 spots x 4 flops, ratio river R / turn-end R per bucket and hand class | Measured; see below. Stored postflop strategies stay turn-end solves |

**Main result (opening widths, % of combos)**

| | UTG | UTG1 | LJ | HJ | CO | BTN | SB | SB limp |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Original build | 9.3 | 11.5 | 14.7 | 16.2 | 25.1 | 37.7 | 46.1 | 4.1 |
| **Improved** | **9.1** | **11.8** | **14.2** | **18.2** | **26.3** | **45.7** | **48.9** | **0.0** |
| Residual swing (last rounds) | +/-0.3 | +/-0.3 | +/-0.4 | +/-0.6 | +/-0.6 | +/-0.6 | +/-1.5 | |

Other changes against the original build: BB vs CO open now 3-bet-or-fold (call 7.1 -> 0.0, 3-bet
10.5 -> 11.1); CO vs a BTN 3-bet no longer calls (14.5 -> 0.1); BB vs SB 3-bets more (28.1 -> 30.3).
Preflop best response: worst seat 0.081 bb/100.

**River correction (item 8).** Realised share of the pot, turn-end model -> river solve:
BTN vs BB single-raised: BTN 0.354 -> 0.451 (R x1.35), BB 0.525 -> 0.401 (x0.77); SB vs BB: SB
0.375 -> 0.343 (x0.88), BB 0.450 -> 0.462 (x1.05); CO vs BTN 3-bet pot: CO 0.324 -> 0.286 (x0.83),
BTN 0.520 -> 0.560 (x1.20). The turn-end model understates how much the player in position and the
preflop raiser realise; this is the main reason the BTN opens 8 points wider than before. Limits:
SB vs BB on 7c5d2h did not fit in memory (19.6 GB compact), so that spot uses 3 flops; BTN vs BB
3-bet pots had no calling range in the start solution (0.04 combos), so 3-bet pots where the caller
is in position, or the 3-bettor out of position, use the single-raised groups with the same
position and role. The river solves are capped at 400 iterations (0.2-2.9% of the pot).

**River error in the stored strategies.** On the river-sampled flops the first decisions (OOP root,
IP after a check) move by **25 points of frequency on average** (worst 74) between the turn-end
and the river solve. The largest single effect: BB leading into a BTN open. The turn-end model
leads 39% on average over the 100 flops (more than 80% on 20 flops; the original build: 33% and
21 flops); with the river played BB mostly checks. Treat BB donk-lead frequencies in
`postflop/srp_*_vs_BB.bin.gz` as an artefact of the turn-end abstraction.

**Rake (output C, `rake_report.md`).** Rake 0 vs rake 10% / 5bb: BTN 53.9 vs 45.7, SB 42.2 vs 48.9,
UTG1/LJ/HJ about 1 point narrower without rake, UTG and CO about the same; BB vs SB open calls
30.3% without rake vs 4.9% with it. Rake makes the blinds fold more (their calls pay the rake
most), so the late-position steals widen with rake in this model and the BTN only tightens against
the rake-free control. The check "rake tightens every non-blind RFI range" fails (it failed in the
original build too).

**Validation (`validation.md`).** Format, packing and round-trip checks pass. Failures: in-position
3-bet-or-fold (no flats; as in the original build), rake-tightening (above), payoff conservation
-0.023 vs 0.02, the four convergence rules (above), and postflop exploitability: 2 of 900 final
solves end above 0.5% of the pot (worst 0.955%). Across all 100bb runs 11 of 17,960 solves hit
the divergence guard; all are in spots whose calling range is almost empty (BTN/CO/BB flatting a
UTG or CO open, 0-0.5% of hands), so their weight in the R fit and the preflop is negligible.

**Compute.** Phase C+D at 100bb: 6 Nov 15:33 - 9 Nov 13:36 UTC, about 70 hours wall clock on 4
cores (~280 core-hours) including the river calibration (~8 h), 10 main rounds, 2 polish rounds,
30 variant rounds and the final pass; about 2 h were lost to three container restarts.

## 1. Game and parameters

All parameters are in `config.json`. The table in the task lists eight seats (UTG, UTG1, LJ,
HJ, CO, BTN, SB, BB) and asks for "each of the 8 positions", so the game is **8-handed**
(choice 1). Blinds 0.5/1, no ante, no straddle, 100bb effective. Rake 10% of the pot, cap 5bb,
no flop no drop, no jackpot drop.

Sizes, exactly as given: open 3bb (SB 3.5bb); SB may complete; 3-bet 3x the open in position,
5x out of position; squeeze +1x (read as a flat +1x regardless of the number of callers, as
the task asks); 4-bet 2.5x in position / 2.8x out of position, all-in when that reaches 40% of
the effective stack; 5-bet all-in. Postflop bets 33%, 66%, 125% pot; raises 2.5x or all-in;
anything reaching the stack is all-in; the out-of-position caller may lead the flop for 33% only.

## 2. Choices made where the task is silent (defaults)

| # | Question | Choice |
| --- | --- | --- |
| 1 | 9-max but 8 seats listed | 8 players, the 8 named seats |
| 2 | BB raise size against an SB limp | 3bb (the open size); the raise is then treated as an open |
| 3 | Which positions count as "in position" for a 3-bet | the 3-bettor acts after the opener postflop (SB and BB never) |
| 4 | Number of players allowed to see a flop | at most 3, and 3-way only in single-raised pots; a call that would make more is not offered |
| 5 | Calls of 3-bets and larger | allowed only while the raiser is the only matched player (keeps 3-bet+ pots heads-up); later players may then only fold or raise |
| 6 | Cold 4-bets and 5-bets | not in the tree: facing a 3-bet or more, a player who has not yet put money in voluntarily may only fold (the blinds count as not voluntary). Tree 60,343 -> 8,057 nodes. |
| 7 | Preflop all-in | raked (the flop is dealt), equity showdown |
| 8 | Postflop raise count | one raise per street (bet, raise 2.5x, then only fold / call / all-in) |
| 9 | Donk lead | only the out-of-position player's first flop decision when it is not the preflop raiser; turn leads use all sizes |
| 10 | "First bet size" for the root decision files | the smallest bet (33%) for nodes 3 and 4 of output B |
| 11 | Rake on a pot won by a fold after a bet | taken from the matched pot; the uncalled bet is returned first |
| 12 | Hand classes reaching the flop under 1% of the time | dropped from the postflop ranges (at most 2.4% of a range's mass; the per-spot figure is `trimmed_mass` in `spots.json`) |
| 13 | Spots where one range is empty in equilibrium | not solved; file written with `"reached": false` (section 6) |
| 14 | R loop not converged to 0.5 points | final R = mean of the last three damped rounds (section 5) |

## 3. Preflop solve

**Tree.** `src/preflop_tree.py`, every sequence allowed by the sizes, depth limit 5-bet,
pruned by choices 4-6: 8,057 nodes (4,242 decisions, 761 flop terminals of which 56 are 3-way,
1,774 all-in terminals, 1,280 fold terminals).

**Hand abstraction.** 169 hand classes. Exact heads-up all-in equity for every pair of the
1,326 combos (all 1,712,304 boards; 47,008 suit/seat-symmetric classes; `src/tools/equity.c`,
25 minutes on 4 cores), reduced to class-vs-class equity and exact combo-pair counts.

**Solver.** Vector-form Discounted CFR (alpha 1.5, beta 0, gamma 2) over the public tree with a
169-vector per player, all 8 seats updated each iteration (`src/preflop.c`). Card removal between
the hero and each opponent is exact (combo-pair counts); opponents are treated as independent of
each other (card removal among opponents, including folded players, is ignored - a standard
approximation, not measured here).

**Exploitability.** Best response of each seat against the average strategies of the others,
with the same payoff model, in bb/100 (final run, 5,000 iterations): UTG 0.007, UTG1 0.113,
LJ 0.031, HJ 0.045, CO 0.058, BTN 0.038, SB 0.048, BB 0.052; **worst 0.113 bb/100** (target 1.0).
Every intermediate run and every rake variant also finished under 0.26 bb/100.

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
restriction, all-in only as a raise, and **an equity terminal at the end of the turn**: when the
turn betting closes, the pot goes to the hands in proportion to their exact equity over the 44
rivers (the task's stated reduction "go to a turn-end equity model", used for every spot). The
river equity per (turn card, hero combo, villain combo) is precomputed and stored exactly as a
count of half-wins out of 88 in one byte. Checked against the solver's exact river sweep on the
same spot: EV within 0.013bb on every combo.

Stop rule 0.5% of the pot (both players' best-response gain), cap 1,000 iterations.

**Flops.** All 22,100 flops reduce to 1,755 strategically different ones; these are clustered
inside texture strata (suit pattern x paired) on high/middle/low rank, connectivity, straight
possibility, broadway count and ace/king high, with clusters per stratum proportional to its share
of all flops (k-medoids, fixed seed). The 100 medoids and their weights are in `flops.json`.
**All 100 flops are solved for every spot** (the user asked not to cut flops).

## 5. Realisation factor R and expected rake

From each solved spot: per hand class, the realised share of the flop pot V(h)/P (flop-weighted,
reach-weighted mean of the per-combo EV, rake included), the expected rake fraction
rho = 1 - (E[V_oop] + E[V_ip]) / P, and R(h) = V(h) / ((1 - rho) P e(h)) with e(h) the hand's
equity against the opponent's flop range. Classes are grouped into 5 strength buckets (equity vs
the opponent's range, reach-weighted quintiles). Spots that were solved use their own bucket R;
every other heads-up flop terminal of the preflop tree uses a least-squares regression per bucket
on (in position, preflop aggressor, log SPR), fitted on the solved spots. Expected rake for other
pots is interpolated on pot size from the solved spots. R by pot type (mean of the fits of rounds
n3-n5, which the final solve averages; "aggressor" = the preflop raiser):

| Pot type | Side | R mean | R by strength bucket (weakest -> strongest) | expected rake, % of flop pot | bucket-model RMSE of the realised share | samples |
| --- | --- | --- | --- | --- | --- | --- |
| SRP | IP aggressor | 1.18 | 1.08 / 1.09 / 1.10 / 1.18 / 1.50 | 18.7 | 0.099 | 12 |
| SRP | IP caller | 0.91 | 0.89 / 0.87 / 0.89 / 0.99 / 0.93 | 20.3 | 0.091 | 3 |
| SRP | OOP aggressor | 1.05 | 0.88 / 1.04 / 1.02 / 0.99 / 1.35 | 20.3 | 0.110 | 3 |
| SRP | OOP caller | 0.78 | 0.77 / 0.86 / 0.74 / 0.94 / 0.81 | 18.7 | 0.084 | 12 |
| 3-bet | IP aggressor | 1.11 | 0.99 / 1.03 / 1.12 / 0.97 / 1.47 | 14.4 | 0.119 | 7 |
| 3-bet | IP caller | 0.84 | 0.99 / 0.68 / 0.89 / 1.05 / 1.00 | 10.6 | 0.055 | 6 |
| 3-bet | OOP aggressor | 1.02 | 0.87 / 0.91 / 0.85 / 1.14 / 1.52 | 10.6 | 0.114 | 6 |
| 3-bet | OOP caller | 0.81 | 0.88 / 0.81 / 0.77 / 0.79 / 1.17 | 14.4 | 0.035 | 7 |
| 4-bet | IP aggressor | 1.08 | 1.03 / 1.06 / 1.00 / 1.10 / 1.20 | 5.7 | 0.086 | 3 |
| 4-bet | IP caller | 0.52 | 0.56 / 0.59 / - / - / 0.67 | 8.0 | 0.041 | 2 |
| 4-bet | OOP aggressor | 1.41 | 1.63 / 1.50 / 1.27 / 1.27 / 1.41 | 8.0 | 0.074 | 2 |
| 4-bet | OOP caller | 0.85 | 0.86 / 0.67 / 0.82 / 0.92 / 0.90 | 5.7 | 0.088 | 3 |

Rake measured, not assumed: a single-raised flop pot of 6.5bb ends with ~1.2-1.4bb of rake (19%
of the starting pot, because the pot grows), 3-bet pots 2.9-3.8bb, 4-bet pots 4.5-4.8bb (the cap).

**Iteration (section 2.5).** Ranges -> calibration solves (14 spots x 12 flops) -> R fit ->
preflop solve, repeated with damping (each new R averaged 50/50 with the previous one) until the
RFI widths move by 0.5 points or less. **The criterion was not met.** RFI widths (% of combos) by
round, UTG / UTG1 / LJ / HJ / CO / BTN / SB:

| Round | Widths | Largest change from the previous round |
| --- | --- | --- |
| stage 3 (fixed R) | 9.8 / 11.6 / 14.3 / 17.7 / 24.5 / 35.9 / 30.2 | - |
| n1 (R fitted on stage 3) | 9.7 / 12.1 / 14.9 / 17.7 / 25.2 / 33.8 / 44.5 | SB +14.3 |
| n2 | 9.9 / 11.9 / 15.5 / 16.6 / 25.6 / 40.8 / 45.0 | BTN +7.0 |
| n3 | 9.6 / 12.3 / 15.1 / 16.8 / 25.9 / 39.8 / 49.9 | SB +4.9 |
| n4 | 9.6 / 11.7 / 15.2 / 17.1 / 25.4 / 36.5 / 45.8 | SB -4.1 |
| n5 | 9.5 / 11.4 / 14.8 / 16.1 / 24.0 / 35.6 / 48.6 | SB +2.8 |
| **final (mean R of n3-n5)** | **9.3 / 11.5 / 14.7 / 16.2 / 25.1 / 37.7 / 46.1** | - |

UTG-CO settle within about 1 point; BTN and SB keep moving by 1-4 points between rounds, mostly
noise from fitting R on 12 flops per spot while the SB and BB ranges swing between 3-bet-or-fold
and flatting. The final solve uses the mean R of rounds n3-n5. **Measured uncertainty of the
final widths from the R loop: about +/-1 point for UTG-CO, +/-2 for BTN, +/-3 for SB.**

## 6. Spots

Final solve (stage 7), all 100 flops per reachable spot, 1,100 solves, ranges from the final
preflop solution. Exploitability in % of the pot, the larger of the two players'.

| Spot | OOP / IP | Pot | Stack | SPR | Range combos OOP / IP | Donk limited | Flops | Exploitability median / worst | Median s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| srp_BTN_vs_BB | BB / BTN | 6.5 | 97 | 14.92 | 43.5 / 499.2 | yes | 100 | 0.475 / 0.500 | 27 |
| srp_BTN_vs_SB | SB / BTN | 7 | 97 | 13.86 | 0.0 / 499.2 | yes | not reached | - | - |
| srp_CO_vs_BTN | CO / BTN | 7.5 | 97 | 12.93 | 330.7 / 0.0 | no | not reached | - | - |
| srp_CO_vs_BB | BB / CO | 6.5 | 97 | 14.92 | 94.3 / 330.7 | yes | 100 | 0.477 / 0.500 | 32 |
| srp_HJ_vs_BB | BB / HJ | 6.5 | 97 | 14.92 | 105.1 / 213.3 | yes | 100 | 0.479 / 0.500 | 41 |
| srp_UTG_vs_BB | BB / UTG | 6.5 | 97 | 14.92 | 79.8 / 123.0 | yes | 100 | 0.480 / 0.499 | 19 |
| srp_SB_vs_BB | SB / BB | 7 | 96.5 | 13.79 | 610.8 / 113.7 | no | 100 | 0.475 / 0.500 | 130 |
| srp_UTG_vs_BTN | UTG / BTN | 7.5 | 97 | 12.93 | 123.0 / 0.0 | no | not reached | - | - |
| 3bp_BTN_vs_BB | BB / BTN | 30.5 | 85 | 2.79 | 258.6 / 12.2 | no | 100 | 0.454 / 0.500 | 8 |
| 3bp_CO_vs_BTN | CO / BTN | 19.5 | 91 | 4.67 | 48.1 / 164.4 | yes | 100 | 0.461 / 0.500 | 7 |
| 3bp_HJ_vs_CO | HJ / CO | 19.5 | 91 | 4.67 | 27.3 / 142.9 | yes | 100 | 0.466 / 0.500 | 8 |
| 3bp_UTG_vs_BB | BB / UTG | 30.5 | 85 | 2.79 | 53.8 / 12.5 | no | 100 | 0.452 / 0.500 | 3 |
| 3bp_SB_vs_BB | SB / BB | 21 | 89.5 | 4.26 | 6.1 / 371.4 | yes | 100 | 0.450 / 0.498 | 4 |
| 4bp_BTN_vs_BB | BB / BTN | 75.5 | 62.5 | 0.83 | 67.9 / 117.1 | yes | 100 | 0.384 / 0.497 | 1 |
| 4bp_CO_vs_BTN | CO / BTN | 51.9 | 74.8 | 1.44 | 87.3 / 0.0 | no | not reached | - | - |

**Worst postflop exploitability reached: 0.500% of the pot** (every solve met the 0.5% target;
the most iterations any solve needed was 510, under the 1,000 cap).

**Not reached (4 of the 15 spots):** in this equilibrium the in-position player never flat-calls:
the BTN 3-bets or folds against a CO, UTG or SB open, and jams or folds against a CO 4-bet.
These are structural results of the solve (rake punishes flatting and rewards winning preflop,
where no rake is taken), not budget cuts. Their files exist with the header, `"reached": false`
and no flops. Several reached spots have very small ranges for one side (BTN calls a BB 3-bet
with 12 combos, SB calls a BB 3-bet with 6): their strategies are equilibrium play for those
narrow ranges and should be read with that in mind.

## 7. Weak points (largest first) and the errors measured

1. **The R model.** One factor per (spot, side, strength bucket) multiplying equity, fitted on 12
   flops per spot. The bucket model reproduces the realised share of each hand class with an RMSE
   of 0.035-0.12 of the pot (table in section 5). R depends on equity only, not on the kind of hand:
   playability (suited connectors, small pairs) is not represented, which is why the BTN opens K9o
   but folds 22 and 87s. The preflop <-> R loop did not reach the 0.5-point criterion (section 5).
2. **The independence approximation is not money-conserving.** Card removal is exact between the
   hero and each opponent, but each player conditions the others' hands on its own cards only, so
   the players disagree slightly on how likely a line is. Measured: with rake 0 and R = 1 the
   eight seats' EVs sum to **+0.066 bb per hand** instead of 0 (+0.128 with the fitted R and rake 0);
   on average under 1 bb/100 per seat. The exploitability figures are measured inside this model,
   so they do not include this bias.
3. **3-way pots.** Equity with a fixed penalty, no 3-way solves (additive 3-way equity model,
   RMSE 0.057 against exact all-ins).
4. **No river.** Every postflop solve ends at the turn with an exact equity showdown; river play,
   river bluffs and river rake growth are not modelled, and the rake is taken from the turn-end pot.
5. **Sparse postflop coverage.** 11 reachable heads-up spots, 100 flops each; only the four root
   decisions are stored.
6. **Pruned preflop tree** (choices 4-6: no cold 4-bets or 5-bets, no multiway 3-bet pots).
7. **Rake variants** (control and sensitivity) use one R round each from the final ranges, so their
   differences from the main run carry the R-loop noise of section 5 (about 1-3 points).

## Validation summary (section 5; full output in `validation.md`)

- Probability sums: raw strategies sum to 1 within 1.2e-7; quantised percents always sum to 100
  and are within 3.3 points of the raw frequency. PASS.
- Round trip with the independent decoder (`src/decoder_ref.py`): 100,000 preflop and 100,001
  postflop entries, 0 differences. PASS.
- RFI width UTG 9.3 < UTG1 11.5 < LJ 14.7 < HJ 16.2 < CO 25.1 < BTN 37.7. PASS.
- BB defence widest against SB (36.7%) and BTN (22.8%); CO 17.6, UTG 10.1. PASS.
- 4-bet and 5-bet narrower than 3-bet (% of all combos): BB 3-bet vs BTN 19.5, BTN 4-bet 8.8,
  BB 5-bet 5.0. PASS.
- **In position, 3-bet narrower than call and polarised: FAIL.** Facing an open, the in-position
  player never flats (BTN vs CO 3-bet 12.5 / call 0.0; CO vs HJ 10.8 / 0.0; BTN vs UTG 6.6 / 0.0;
  HJ vs UTG 6.1 / 0.0): a linear 3-bet-or-fold strategy, so there is no call range to compare with.
  This is the solve's answer under 10% / 5bb rake with no flop no drop, not a packing error. (Out
  of position the BB 3-bets 19.5 and calls 3.3 against the BTN.)
- **Rake tightens every non-blind RFI range vs the rake-free control: FAIL for LJ only** (14.7 vs
  13.8, within the R-loop noise); UTG -0.2, UTG1 -0.6, HJ -2.2, CO -1.3, BTN -7.7 points.
- Hand sanity table for AA, KK, AKs, AKo, 22, 87s, A5s, K9o, 72o in the 8 RFI spots and BB vs BTN:
  `validation.md` section 4.
- Exploitability: preflop worst 0.113 bb/100; postflop worst 0.500% of the pot. PASS.
- Sensitivity (rake cap 3bb and rake 5%, BTN / CO / BB spots): `rake_report.md`. Lower rake
  widens the BTN (37.7 -> 44.9 with cap 3, 45.7 with 5%) and CO (25.1 -> 26.2); the BB keeps a
  3-bet-or-fold strategy against the BTN (3-bet 19.4-19.7, call 0.0-0.3) and calls the SB less.
  The c-bet on the wet 9c7c6d single-raised BTN vs BB flop falls from 65% to 60% (rake 5%) and
  56% (rake 0).
- Published GTO charts were not used for comparison: no numbers are quoted from memory.

## 8. Licences

All code in `src/` is original to this repository (the postflop solver core is the ICMKit solver
written earlier in this repository). No third-party solver, evaluator or library code is included;
the only dependencies are the C standard library, POSIX threads, OpenMP (gcc), Python 3 and numpy
(BSD licence).

## 9. Results, sizes and commands

**Outputs** (total 1.37 MB, under the 15 MB target):

| File | Size |
| --- | --- |
| `preflop.json` (149 records, reach > 0.1%) | 59,157 bytes |
| `preflop_ev.bin` (149 x 169 float16) | 50,362 bytes |
| `postflop/*.bin.gz` (15 files, 11 with 100 flops) | 1,195,945 bytes |
| `test_vectors.json` (300 + 300) | 119,726 bytes |
| `rake_report.md`, `validation.md`, `flops.json`, `combos.json` | 2 KB, 4 KB, 35 KB, 18 KB |

**Compute.** Wall clock 14:18 - 23:30 UTC, about 9.5 hours. CPU: postflop solves 18.8 core-hours
(2,403 solves: calibration rounds, the final 1,100-flop pass and the three rake variants), preflop
solves about 10 core-hours (14 runs of 3,000-5,000 iterations, 0.2 s per iteration on 4 cores),
exact equity table 1.5 core-hours; **about 30 core-hours in total**.

**Output formats.**
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

**Commands** (from `nlh9max/`):

```
make                                   # build/equity, preflop, postflop, test_rake
make test                              # rake unit tests (C and Python)
./build/equity work                    # stage 1: exact equities (25 min, 4 cores)
python3 src/preflop_tree.py config.json work/tree.json
python3 src/flops.py 100 flops.json
python3 src/rmodel.py fixed work       # stage 3 priors
./build/preflop work 3000 work/s3/pre work/rfile.bin 4
python3 src/spots.py work/s3/pre work/s3/ranges
python3 src/run_postflop.py work/s3/ranges work/s4/solves calib all 4     # stage 4
python3 src/fit_r.py work/s3/ranges work/s4/solves work/s4/fit.json         # stage 5
sh src/iterate.sh work/n1 work/n2      # one preflop <-> R round (n2 ... n5)
# final: mean R of n3-n5 -> work/final/rfile.bin, then
./build/preflop work 5000 work/final/pre work/final/rfile.bin 4
python3 src/spots.py work/final/pre work/final/ranges
python3 src/run_postflop.py work/final/ranges work/final/pre_solves all all 4   # stage 7
sh src/variant.sh rake0 0 0; sh src/variant.sh cap3 0.10 3; sh src/variant.sh pct5 0.05 5
python3 src/pack.py preflop work/final/pre .
python3 src/pack.py postflop work/final/ranges work/final/pre_solves .
python3 src/validate.py . work/final/pre work/final/ranges work/final/pre_solves work/var_rake0/pre
python3 src/rake_report.py rake_report.md work/final/pre work/var_rake0/pre "cap 3bb=work/var_cap3/pre" "rake 5%=work/var_pct5/pre"
```
