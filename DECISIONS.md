# Part B decisions

Decisions taken while building Part B unattended. Newest sessions append at the end.

## B1 (C engine), 2026-10-28

### Repository layout
- The repo root already holds an unrelated PLO6 project (`PROGRESS.md`, `REPORT.md`, `src/`, `app/`).
  It is left untouched. Part B progress is logged in `ICMKit/PROGRESS.md` instead of the root
  `PROGRESS.md`; this file (`DECISIONS.md`) is new and sits at the root.
- `tools/`, `tests/`, `config/`, `ranges/` and the `Makefile` are at the repo root, as in the
  spec's layout table. Build output goes to `build/` (and `build-asan/`), both git-ignored.
- `Package.swift` uses `swift-tools-version:6.0` (needed for `.iOS(.v18)`) and an explicit
  `module.modulemap` with umbrella header `icmcore.h`. The `-O3` unsafe flag is release-only; if
  Xcode rejects unsafe flags for a package dependency, delete that line (Xcode's Release
  optimisation is enough).

### Cards and combos
- Card = rank * 4 + suit, rank 0..12 = 2..A, suit 0..3 = c, d, h, s.
- Combo index = hi * (hi - 1) / 2 + lo over the two card numbers (0..1325).

### ICM
- Malmuth-Harville computed with a subset dynamic programme (O(2^n n), n <= 9) in doubles.
- A player with a zero stack (busted in a terminal state) finishes below everyone with chips;
  several zero stacks share the prizes of the places they occupy equally. Places beyond the
  payout list pay 0.

### Hand evaluator
- Own design: a 13-bit flush table (8192 entries) plus tables of the best non-flush hand for
  every multiset of 5, 6 or 7 ranks, addressed by a lexicographic rank of the per-rank counts
  (6,175 / 18,395 / 49,205 entries). Strength 1..7462, higher is better. Tables are static arrays
  filled once with `pthread_once`; read-only afterwards. About 26M seven-card hands/s single
  threaded here (full enumeration 5.2 s).
- Fast subset kept for later sessions (`make test`): all 2,598,960 five-card hands against the
  standard counts, 200,000 random 7- and 6-card hands against brute force over subsets, spot
  checks. The full enumeration is `make test-full` (`build/test_handeval --full`).

### Flop classifier
- The ace counts high only for the "connected" rule (A-2-3 is not connected: 14 - 2 = 12).
  None of the spec's representative flops depends on this.
- All 18 representative flops land in their listed class; the list is unchanged.
- Share of all 22,100 flops: dry high 21.7%, dry low 3.0%, paired 17.2% (includes trips),
  two-tone 37.1%, monotone 5.2%, connected 15.7%.

### Risk premium and matchup buckets (`config/buckets.json`, `tools/buckets.c`)
- Definition used: hero calls an all-in for the effective stack min(hero, villain) with no other
  money in the pot (no blinds, antes or dead money); the other four stacks stay fixed.
  ICM break-even equity p* = ($fold - $lose) / ($win - $lose); risk premium = p* - 50%
  (the chip-EV break-even). This is the usual textbook definition and is symmetric in setup for
  both players, so a pair (i, j) gives the point (RP(i vs j), RP(j vs i)).
- 60 ordered pairs (30 per table). Quartiles use linear interpolation (numpy's default):
  thresholds 3.59%, 6.98%, 10.11%. Bucket b holds premiums in [t(b-1), t(b)); bucket 3 is >= 10.11%.
- Bucket centre = midpoint of its range, with the smallest and largest observed premium
  (0.97% and 19.25%) as the outer edges. The pair chosen for a cell is the one, among the pairs
  whose (hero, villain) premiums fall in that cell, with the smallest Euclidean distance to the
  cell centre.
- Values were cross-checked against an independent brute-force Python ICM (max diff 5e-7).
- **Skipped cells: (0,0) and (2,2).** No real pair has both premiums in the lowest quartile
  (two players are rarely both cheap to risk against each other: the short stacks are cheap for
  the big stacks but expensive the other way round), and no pair falls in (2,2). 14 cells remain,
  so each ICM tier has 14/16 of the spec's counts (e.g. T1 = 336 ICM + 24 chip EV solves).
- Stack roles at both tables: leader (runaway at table 1, chip leader at table 2), 40/30/25 and
  50/35/30 are mid stacks, 15 and 10 are short stacks.

### Street solver (`streetsolver.c`)
- **Algorithm:** Discounted CFR (alpha 1.5, beta 0, gamma 2, Brown & Sandholm's recommendation)
  with alternating updates, vector form over the public tree. The discount for iteration t-1 is
  applied lazily when a node is next updated (equivalent to discounting at the end of the
  iteration). All solver arithmetic is float32; ICM itself runs in double.
- **Ranges:** per-player lists of live combos (weight > 0, not blocked by the starting board).
  Combos blocked by later cards keep their slot; their reach is zeroed at the chance node and
  their value is excluded when the chance node combines its children.
- **Chance nodes:** one child per dealt card, weighted 1/45 (turn) and 1/44 (river): every pair of
  compatible hands sees exactly that many cards. All-in before the river is a terminal that
  enumerates the runout (44 rivers, or C(45,2) = 990 turn-river pairs) with the same O(n)
  showdown, instead of building chance nodes below it.
- **Suit isomorphism:** a suit permutation is used when it maps the starting board onto itself,
  leaves both range vectors unchanged, and fixes every card dealt so far. Cards that map onto
  each other share one child; the values for the other cards are the stored child's values with
  the combo permutation applied. With asymmetric input ranges isomorphism switches itself off.
  (Rainbow flops have no symmetry; two-tone and monotone flops do.)
- **Showdown:** for every possible final board, each player's live combos sorted by strength. One
  sweep per showdown: cumulative sums for strictly weaker opponent combos, a short pass per group
  of equal strength, and 52 per-card sums to remove combos sharing a card; O(n) per terminal.
- **Tree:** exactly the spec's: bet 33% / 75% / all-in; facing a bet fold, call, raise to 3x the
  bet, all-in; one raise per street, after that fold / call / all-in. Bet and raise sizes are
  "to" amounts on the street; 33% and 75% are of the pot before the bet. The 67% rule compares
  the chips left behind after the bet is called with the pot after the call. Duplicate sizes
  (for example 75% turning into all-in) are merged. Both players always have equal effective
  stacks (the spot's effective stack).
- **Payoffs:** every terminal is one of win / lose / tie for given total investments (i0, i1),
  so payoffs are computed once per distinct (i0, i1) and shared (a few hundred ICM evaluations per
  tree). In ICM mode the two players' final stacks (stack behind - invested + share of the pot)
  replace their seats at the full table; other stacks are fixed. Each player's payoffs are
  shifted by a constant (their value when giving up the starting pot), which changes nothing in
  the solve but keeps float32 values small.
- **Exploitability:** for each player, best-response value minus value under the average
  strategies (both against the opponent's average strategy), as a percentage of that player's
  "pot value" = payoff(win the starting pot now) - payoff(lose it now). In chip mode the pot value
  is the pot. The stop rule needs both players under the target. One walk per player computes
  both numbers; checks run every `check_every` (default 10) iterations.
- **Threads:** a pthread fork-join pool; the first chance level a walk meets is processed in
  parallel (turn cards for flop solves, river cards for turn solves). Results are combined in a
  fixed order, so 1 and 4 threads give identical results. Solver objects hold all state; there is
  no global mutable state except the hand-evaluator tables, built once under `pthread_once`.
- **Memory:** all solver allocations go through a counter; `ss_memory_required()` and
  `ss_result.peak_memory` report the peak. `memory_limit` makes `ss_create` fail before the big
  allocations if the estimate is over the limit.
- **Test 4 criterion:** with winner-take-all payouts ICM is an affine function of chips, so the
  games are strategically identical, but strategies of indifferent or barely reached combos are not
  unique and drift apart with float rounding. The test therefore checks (a) values equal as a
  fraction of the pot, (b) the ICM solution's exploitability when played in the chip-EV game is
  within the 0.5% tolerance, and (c) reach-weighted strategy distance at well-reached nodes is
  small (it is 10x larger with real payouts, which shows the test can tell them apart).
