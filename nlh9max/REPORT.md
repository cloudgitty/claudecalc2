# NLH 8-handed, 100bb, high-rake live cash: approximate equilibrium strategies

**What this is.** An approximate equilibrium of an *abstracted* game, not an exact solution of
no-limit hold'em. The preflop game is solved with a payoff model for the postflop play; the
postflop game is solved heads-up on a sample of flops, to the end of the turn, with the river
replaced by an equity showdown. Every abstraction below is stated with the error measured for
it, where it could be measured. The numbers marked `[final]` are filled in from the final run
(section 9); `PROGRESS.md` has the stage log.

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
| 13 | Spots where one range is empty in equilibrium | not solved (listed in section 6) |

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
with the same payoff model, in bb/100: `[final]`.

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
pots is interpolated on pot size from the solved spots. R tables by pot type: `[final]`.

**Iteration (section 2.5).** Ranges -> calibration solves (14 spots x 12 flops) -> R fit ->
preflop solve, repeated with damping (each new R averaged 50/50 with the previous one) until the
RFI widths move by 0.5 points or less: `[final]`.

## 6. Spots

`[final]` table: spot, pot, stack, SPR, range sizes, flops solved, exploitability.
Spots with an empty range in equilibrium (not solved): `[final]`.

## 7. Weak points (largest first)

1. **The R model.** One factor per (spot, side, strength bucket) multiplying equity; fitted on
   12 flops per spot during the iteration. Bucket model error (RMSE of the realised share):
   `[final]`.
2. **3-way pots.** Equity with a fixed penalty, no 3-way solves.
3. **No river.** Every postflop solve ends at the turn with an equity showdown; river play,
   river bluffs and river rake growth are not modelled. Rake is taken at the turn-end pot.
4. **Sparse postflop coverage.** 14 heads-up spots, 100 flops each; turn and river only through
   the solve trees, not stored.
5. **Independent opponents** in the preflop model (no card removal among opponents).
6. **Pruned preflop tree** (choices 4-6).

## 8. Licences

All code in `src/` is original to this repository (the postflop solver core is the ICMKit solver
written earlier in this repository). No third-party solver, evaluator or library code is included;
the only dependencies are the C standard library, POSIX threads, OpenMP (gcc), Python 3 and numpy
(BSD licence).

## 9. Results and commands

`[final]`
