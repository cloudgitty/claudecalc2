**Read this first.** These ranges are a self-consistent set of best responses inside a simplified
model: heads-up decisions only, postflop replaced by realised equity `R * eq * pot`. They are **not a
solver equilibrium**. The opening ranges come out much tighter than real PLO6 play (LJ about 5.5%,
BTN about 20%), and the defending ranges much wider (BB continues 69 to 76%). The reasons are under
"Known weak points". Treat the output as a model result to review, not as ground truth.

## Parameters

- PLO6, 6-max, 200bb effective, pot-limit, exactly two hole cards + three board cards. No rake, no ante.
  Blinds 0.5/1.
- Sizes: open 3bb (SB to 4). 3-bet to 10.5 in position, 12 from the blinds. Pot-sized 4-bet
  (`F = 3T + dead`: 33 vs 10.5, 36.5 vs a BB 3-bet with the SB dead, 36 SB vs BB, 37 BTN vs an SB
  3-bet). The 5-bet is all-in (200).
- Every heads-up confrontation (opener O, responder D) is solved as a five-node tree: O1 open or fold;
  D1 fold, call or 3-bet; O2 fold, call or 4-bet; D2 fold, call or jam; O3 fold or call the jam.
  All 15 (O, D) pairs are solved, including responders with no output spot (HJ/CO/BTN/SB vs earlier
  opens), because the opener's RFI EV needs every seat behind it. 24 nodes are output.
- RFI EV = P(all fold) * 1.5 + sum over responders in seat order of P(earlier ones fold) *
  [P(call) * EV(called) + P(3-bet) * EV(best response to the 3-bet)].
- Realisation: `R = R0 * (1 + 0.15 z)`. z is the playability z-score (nut-made wins minus
  straight-or-better losses vs that villain range), clipped to +-2 and standardised over the
  combos reaching the decision. All-in pots use R = 1.
- R0 used in the solve: 1.00 IP / 0.82 OOP in single-raised and 3-bet pots, 0.90 IP / 0.82 OOP
  in 4-bet pots. Cold calls get a players-behind factor 0.878^k (see below), which gives the SB its 0.72.
- Mixed frequencies: linear ramp over +-1% of reaching combos around each indifference point,
  rounded to 5%. Damping 0.5. Round cap 12, plus a warm-up and refinement rounds (see below).

## Assumptions changed or added (every deviation from the prompt)

1. **R0: calibrated, then not used. This is the biggest deviation.** Stage 4 measured R0 with
   40,000 flop-to-river simulations per configuration under a fixed, identical-for-both-seats
   equity-bucket strategy. Measured: SRP OOP 0.810 / IP 1.177, 3BP 0.856 / 1.157, 4BP 0.826 / 1.175
   (`work/r0.json`). A first strategy (absolute equity thresholds vs the unconditioned range) gave
   R_oop 0.0 to 0.4 at SPR 30. That is an artefact of value-only betting and was discarded. The
   percentile strategy that replaced it still gives 1.26 / 0.73 in a control run with identical
   ranges on both seats. So the measurement mostly reflects how exploitable the fixed strategy is,
   not real positional value. Solving with the measured values unravelled the game: with IP R0 about
   1.17, every IP call or 3-bet with about 40% equity is profitable. PLO6 equities cluster around
   40 to 60%, so BTN 3-bet 33% of all hands vs CO and the opens collapsed to LJ 1.5 / HJ 2.2 /
   CO 4.7 / SB 1.9 while still falling. The final solve therefore uses the prompt's default R0. The
   measured values stay in the report. The prompt's "measured, not guessed" requirement is **not met**.
2. **Players-behind factor for cold calls.** The prompt's SB default (0.72 vs 0.82 OOP, "BB behind")
   is a penalty of 0.72/0.82 = 0.878 for one player left to act. It is applied to every flat call of
   an open, once per player left behind the caller: HJ 4, CO 3, BTN 2, SB 1, BB 0. So BTN flats vs
   CO with R0 0.77 instead of 1.00. Without it, IP cold-callers in the heads-up model call 60 to
   87% and early opens collapse (LJ 4.4% and still falling in a test run).
3. **Census instead of a stratified sample.** The engine runs 12M trials/s, so all 962,988 classes
   were simulated at 4,000 boards against each of 12 reference bands, not a stratified 80,000.
   The feature model and its holdout validation are still built as specified. EB shrinkage applies
   to every class.
4. **Reference villain ranges = 12 strength bands, combined as mixtures.** The bands are
   combo-percentile slices 0-1.5, 1.5-3, 3-5, 5-8, 8-12, 12-17, 17-23, 23-30, 30-40, 40-55, 55-75 and
   75-100%. Strength order = equity vs the top 25% (pilot simulation). The equity against any range
   during the iteration is the band-weight mixture of the per-band equities. Each band equity
   includes exact card removal; the mixture weights do not adapt to the hero's cards. The borderline
   re-simulation then replaces this approximation with exact equities for every class near a cutoff.
5. **Warm-up before round 1.** Starting all nodes from crude initial responses made the
   simultaneous best-response update oscillate (BB 3-bet 60%+, LJ open 60%). The response nodes are
   first iterated against the fixed starting opens (14 sweeps), then the 12 counted rounds run.
6. **Refinement rounds after the re-simulation.** Six more damped rounds with the exact equities
   in place. These come on top of the 12-round cap.
7. **Fold equity with card removal.** Villain action probabilities given the hero's hand are
   computed by sampling villain hands that do not collide with the hero (48 per class), averaged
   within the hero's rank multiset to remove noise. Suit-level blocking of fold frequencies is
   therefore ignored (it is kept in the equities).
8. **Borderline band narrowed.** The plan was +-2% of reaching combos. Narrowed to +-1.25% (RFI too)
   to stay inside the 60-minute re-simulation budget. A few thousand (class, range) tasks whose
   range almost always collides with the hero's cards (fewer than 6,000 valid trials out of 12,000)
   are dropped and keep the mixture value.
9. Unreachable classes get byte 0 (prompt rule). O2 spots are masked by "open > 0" and D2 spots by
   "3-bet > 0". The frequency stored is the decision frequency conditional on reaching the node.

## Method

1. Stage 1: enumerate all C(52,6) hands in C, canonical code per the reference algorithm (verified
   against the Python reference on 3,000 random hands), `np.unique`. Combos are checked against
   24/|stabiliser| on 3,000 classes.
2. Stage 2: Omaha evaluator (15 hole pairs x 10 board triples via a 91 x 455 rank table plus a
   flush table), checked against an independent brute-force evaluator. Villain pools are 400,000
   concrete hands sampled by weight, with rejection on collisions. Each trial picks a uniformly random
   suit representative of the hero class, so class equity is the mean over its representatives.
3. Stage 3: LightGBM on 65 features (rank counts, suit pattern, per-suit top ranks, nut / K / Q
   flush draws, pairs and trips, straight windows and straight-capable pairs, runs, span, danglers,
   high-card sums, sorted ranks). 5-fold cross-fit, so each fold is a 20% holdout. Empirical-Bayes
   posterior per class: `(y/se^2 + m/tau^2)/(1/se^2 + 1/tau^2)`, with tau^2 = holdout MSE minus
   mean simulation variance.
4. Stage 4: R0 calibration (see the assumptions above).
5. Stage 5: iteration. Stage 6: borderline re-simulation vs the actual ranges (exact removal,
   adaptive trials until SE <= 0.3 pts), corrections = exact minus mixture, refinement rounds.
6. Stage 7: quantise to 5%, pack, widths, test vectors. Stage 8: checks and this report.

<!--AUTO-->

## Known weak points (read before using the ranges)

- **Tight opens, very wide defence.** The rule "continue if R * eq >= pot odds" plus PLO6's compressed
  equities (vs a 5% range, the median hand still has about 40%) makes BB defend 69 to 76% and the
  blinds' and cold-callers' ranges wide. Openers then rarely win the blinds, so RFI shrinks
  (LJ 5.5%, BTN 20%). Real PLO6 opens are much wider. The heads-up-only model, with no multiway
  pots, squeezes or postflop play, is the main cause. The players-behind factor only partly
  compensates.
- **R0 not measured** (assumption 1). Every width is sensitive to R0; see the sensitivity table.
- **Convergence is borderline, not clean.** See the convergence log. The last refinement round is
  inside tolerance, but the "two consecutive rounds" rule was not met within the caps. Some
  round-to-round noise comes from the Monte Carlo card-removal response probabilities.
- **The band mixture is biased.** Measured on the borderline classes against the actual final
  ranges, it is about -0.8 pts (exact equities are higher), with RMSE about 1.5 pts. Classes in the
  borderline band carry exact corrections; classes outside it rely on the mixture. Their decisions
  are far from a cutoff, so a 1 to 2 pt error rarely flips them, but some mid-range frequencies
  outside the band can be off.
- **Playability (z) model** is weak: noise-corrected R2 is much lower than for equity, and it
  enters R at +-30% at most.
- **Opener vs 3-bet continues with almost the whole opening range**, and BB jams over 4-bets with
  most of its 3-bet range. Both follow from R0 IP 1.0 in 3-bet pots and the tight opening ranges,
  and are probably too loose relative to a real solver.
- **BTN vs CO** 3-bets more often than it flats, because the players-behind factor applies to flats only.
- No published PLO6 solver numbers were available to reproduce here, so the external comparison
  covers first-principles identities and exact enumeration only. No numbers are quoted from memory.
