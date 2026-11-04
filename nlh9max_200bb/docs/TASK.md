# Task: compute NLH 9-max GTO-style preflop and postflop strategies for a high-rake live cash game at 200bb, for a trainer app

You are a coding agent in a cloud session that spends session credits, on a CPU-only container (no GPU; limited disk, RAM and wall-clock; the container can be reclaimed when idle). Use numpy plus compiled code (numba, C or Rust, whichever installs) and every CPU core. Open-source tools are allowed (for example a postflop CFR solver library, an equity/evaluator library) if they install; check licences and list them in REPORT.md. You may run for hours. Do not ask me questions: where something is ambiguous pick the stated default and list every such choice in REPORT.md. Be honest about every approximation: the result is an approximate equilibrium for an abstracted game, not an exact solution of full NLH 9-max, and REPORT.md must say so and quantify the errors you measured.

## 1. Game definition (defaults, all parameters in one `config.json`)
- No-limit hold'em, 9-max, seats UTG, UTG1, LJ, HJ, CO, BTN, SB, BB. Blinds 0.5/1 bb, **no ante, no straddle**. Effective stack **200bb** for all players (defaults; stack is a config parameter, I may ask for other depths later).
- **Rake (high, live style): 10% of the pot, capped at 5bb, no flop no drop.** Raked pots are those that see a flop. A hand that ends preflop is not raked. Rake is taken from the final pot when the hand ends postflop (showdown or fold), after any all-in. Parameters in config: `rake_pct=0.10`, `rake_cap_bb=5`, `no_flop_no_drop=true`, `jackpot_drop_bb=0`. The cap binds from a 50bb pot, so rake matters a lot: it makes every raised pot's effective stakes smaller, makes calling and bluffing marginal hands worse, makes out-of-position play worse, and pushes ranges toward fewer calls, more 3-bet-or-fold and more folds in the blinds. The solves must model this, not just subtract an average.
- Bet sizes (fixed, in config; these are my numbers, use them exactly):
  - **Open (RFI):** 3bb from every seat except SB, which opens to 3.5bb. SB may also complete (limp) as a solver option; flat-calling is allowed everywhere. No second open size.
  - **3-bet:** 3x the open when in position, 5x the open when out of position (blinds are out of position). With 3bb open that is 9bb IP and 15bb OOP.
  - **Squeeze** (open plus at least one caller, then a raise): the 3-bet multiple for the squeezer's position plus 1x, i.e. 4x the open in position and 6x out of position. State in REPORT.md that this was read as a flat +1x regardless of the number of callers.
  - **4-bet:** 2.5x the 3-bet when in position, 2.8x the 3-bet when out of position ("in position" is judged between the two players left in the pot). Cap at all-in if the size reaches 40% of the effective stack or more.
  - **5-bet:** all-in.
  - **Postflop (flop, turn and river):** bets of 33%, 66% and 125% of the pot; raises 2.5x the bet faced or all-in; any bet or raise that reaches the effective stack becomes all-in. Donk bets (out-of-position caller leading into the preflop raiser) are allowed on the flop with the 33% size only. Nothing else is in scope.

- **What changes at 200bb (model it, do not copy 100bb behaviour):** the rake cap (5bb) binds from a 50bb pot, which now happens in most 3-bet and 4-bet pots, so rake is a much smaller share of deep pots than of 100bb pots: single-raised pots (about 6 to 8bb) are still heavily raked proportionally, while stacked-off pots lose only 5bb of 400bb. Do not reuse any 100bb result. Typical SPRs: single-raised about 30 to 35, in-position 3-bet pot about 10, out-of-position 3-bet pot about 6, 4-bet pot about 2 to 3. With these depths three streets of 125% bets matter, so keep the bet tree exactly as listed but let every node that would put more than 60% of the remaining stack in on a non-river street be an all-in option as well as the listed size. The 4-bet cap rule ("reaches 40% of the stack") will rarely trigger at 200bb; keep it but report how often it did.

## 2. Preflop solve (9 players) 
A full 9-player postflop tree is impossible, so use this scheme and document it:
1. Hand abstraction: 169 hand classes for preflop strategies (suit isomorphism). Equity vs hand class pairs: exact 169x169 table (vs a specific hand class) for heads-up, with a card-removal-correct combo count matrix.
2. Preflop game tree: all players, the action sequences allowed by the sizes above, with a depth limit of 5-bet. Players that fold are removed. Terminal nodes: (a) all fold to one player; (b) a showdown/postflop node among 2 or 3 players. For nodes with 3 or more players seeing a flop, use a realised-equity model (see 4); heads-up nodes use the same model calibrated to the postflop solves (see section 3).
3. Solver: external-sampling MCCFR (or CFR+ on a pruned subtree if tractable), with average strategy output, over all 8 positions at once. Run until exploitability proxy (best response against the average strategy in each position, evaluated with the same payoff model) is <= 1.0 bb/100 for each of the 8 positions in the root decision, or the compute budget ends. Report the numbers actually reached.
4. Payoff model for postflop nodes: payoff = equity share of the final pot x realisation factor R(position, pot type, players, SPR) minus rake, where rake follows section 1. R is **measured**, not guessed: from the postflop solves of section 3, regress R on (position pair, pot type, SPR, hand class strength bucket) for heads-up; for 3-way use the heads-up value times a multiway penalty fitted on a sample of 3-way flop solves (if the 3-way solves are not affordable, use equity-only with a stated fixed penalty and flag it as a weak point). Include the effect of rake explicitly in R: re-fit R with rake on. Report R by pot type.
5. Iterate preflop ↔ R: re-fit R with the new ranges and rerun the preflop solve, twice, until RFI widths per position change by <= 0.5 points.

## 3. Postflop solves
Pot types (heads-up, six or more position pairs each):
- Single-raised pots: opener vs caller for BTN vs BB, BTN vs SB, CO vs BTN, CO vs BB, HJ vs BB, UTG vs BB, SB vs BB (SB open, BB defend), plus one "early open, late call" pair (UTG vs BTN).
- 3-bet pots: opener vs 3-bettor for BTN vs BB (3-bet), CO vs BTN, HJ vs CO, UTG vs BB, SB vs BB.
- 4-bet pots: BTN vs BB, CO vs BTN.
Ranges at the flop come from the preflop strategy of section 2 (reach probabilities per hand), not invented. Stack at the flop follows from the preflop sizes.

Flop set: choose about 100 representative flops by clustering the 1,755 isomorphic flops on features that matter (high card rank, paired, suitedness, connectivity, range-advantage proxy). Provide the list and weights in `flops.json`. Solve each (pot type × pair) × flop to the river with the bet-size set above (33%, 66%, 125%), to exploitability <= 0.5% of pot, or the budget; for pairs where this is too expensive solve flop-only with the equity-model terminal at the end of the turn (state it clearly and use the same rule for all). Rake is part of the postflop payoff: any terminal where a pot is awarded is reduced by min(10% x pot, 5bb), so rake changes thin value bets, bluff frequencies and calls.

## 4. Outputs (compact; I will implement a loader, so follow the formats exactly)
Target total <= 15 MB, hard cap 25 MB; choose the quantisation below to fit, and state the sizes.

A. `preflop.json`: one record per decision node reached in the equilibrium (with reach > 0.1%): 
`{"id": "UTG:open", "path": ["UTG:r2.5","UTG1:f",...], "hero": "BTN", "actions": ["f","c","r10","a"], "reach": 0.0123, "hands": "AA=r100 AKs=r85,c15 ..."}`
Use my existing string format for `hands`: hand label + `=` + action code and percent, comma separated (a = all-in, r = raise/bet, c = call/check, f omitted; multiple raise sizes use `r1`, `r2`, `r3` mapped by `actions`); 169 labels, hands with 0% omitted, frequencies as integers in 5% steps summing <= 100 (folds implied). Also include `ev_bb` per hand class (float16 array in the file `preflop_ev.bin`, 169 values per node, in the order of the nodes).

B. `postflop/<pottype>_<hero>_vs_<villain>.bin.gz`: for each flop in `flops.json`, the **root decisions** only: (1) first player's action (check or bet size) at the flop root, (2) second player's response to a check (check or bet size), (3) second player's response to the first bet size (fold/call/raise), (4) first player's response to a check-bet (fold/call/raise). Per combo (1,326 combos in a fixed enumeration order given in `combos.json`) store one byte per action at the node, quantised to 5% steps as an index into the table of (frequency pairs) given in the header; zeros for combos not in the range. Header: node list, action lists, flop cards, reach per combo as uint8 (0-255), pot and stack in bb, SPR. Deflate level 9. 

C. `rake_report.md`: for each spot, the effect of rake: widths with rake 0 vs the 10%/5bb solve for the same tree (RFI widths for the 8 positions, BB defend vs BTN, 3-bet width of BB vs BTN, c-bet frequency on a dry and a wet flop, overall).

D. `test_vectors.json`: 300 random (node, hand class) pairs for A, and 300 random (spot, flop, combo, node) tuples for B, each with the expected quantised result, for checking my port.

E. `REPORT.md`, `config.json`, `flops.json`, `combos.json`, `src/` (reproducible code and exact commands), `PROGRESS.md`.

## 5. Validation you must run and report
- Probability checks: all strategies sum to 100% (before quantisation, within 1e-6; after, within 5 points per hand and normalised so call+raise+fold = 100).
- Round trip: decode your own files with an independent decoder written from this spec and compare 100,000 random entries.
- Monotonic and structural sanity: RFI width UTG < UTG1 < LJ < HJ < CO < BTN; BB defence is widest vs BTN and SB open; 3-bet range polarised and narrower than the call range in position; 4-bet and 5-bet ranges narrower than 3-bet ranges; rake makes every non-blind range tighter than the rake-free control run.
- Hand sanity (list the frequencies): AA, KK, AKs, AKo, 22, 87s, A5s, K9o, 72o in each of the 8 RFI spots and in BB vs BTN open.
- Exploitability or best-response numbers actually reached, per solve, and the worst one.
- Sensitivity: rerun the BTN, CO, and BB spots with rake_cap 3bb and with rake 5% to show the direction and size of the change.
- Compare with published GTO charts or solver statistics only if you can reproduce them from first principles; never quote numbers from memory as facts. Flag the weakest parts in REPORT.md (3-way pots, R model, the sparse postflop coverage).

## 6. Working inside a cloud session (credits and limits)
- Work in stages with a one-line status in `PROGRESS.md` after each: (1) tree and equity tables, (2) rake model and unit tests, (3) first preflop solve without postflop detail (fixed R), (4) postflop solves for the calibration sample, (5) fit R with rake, (6) final preflop solve, (7) full postflop solves, (8) packing, validation and report. Save intermediates under `work/` so a stage can resume after a container restart.
- Measure throughput after the first minute and write the sample sizes you can afford before committing (flops per pot type, iterations, boards). Reduce in this order if the budget is exceeded: number of flops (not below 30; 200bb trees are larger, so expect to need this), river detail (go to a turn-end equity model), the number of postflop pairs (keep BTN vs BB single-raised, 3-bet and CO vs BTN first). Record each reduction and its accuracy impact in REPORT.md. Never reduce section 5.
- Disk: store float16 where possible and delete scratch files; stay under a few GB.
- At the end print total wall-clock time, CPU-hours and output file sizes.

## 7. Do not
- Do not change names, file layouts or the combo enumeration after I receive `combos.json`.
- Do not hard-code range widths from memory; they must come out of the solve.
- Do not skip the rake in any payoff or sneak in a lower rake to speed things up.
