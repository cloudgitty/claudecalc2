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

### Hand categories (`handcat.c`, for the flop summaries in Part B2)
- Order of precedence: made hands first, then draws, then air. A pair or better that uses a hole
  card is never counted as a draw (top pair + flush draw is "top pair").
- "Sets and better / two pair": sets, trips (hole card matching a board pair), two pair using
  both hole cards, straights, flushes, full houses, quads.
- "Top pair": a hole card pairs the highest board card, or a pocket pair above it (overpair).
  On a paired board, pairing the unpaired card counts as top pair only if that card is the
  highest rank (5s5dAc + Ax), otherwise middle/bottom pair.
- "Middle and bottom pair": a hole card pairs a lower board card, or an underpair.
- A board pair alone is not the player's pair.
- Draws: outs = turn cards that give a straight or flush (or straight flush) the player does
  not already have. 8 or more = strong draw, 1-7 = weak draw. Backdoor draws and overcards are
  not outs, so on dry rainbow boards most unpaired hands are "air".

### Placeholder preflop ranges (`ranges/`)
The repo has no preflop solver, so these six files are rough placeholders typical of tournament
play at each depth, all marked `"placeholder": true`. Out of position = big blind, in position =
button. In 3-bet pots the big blind is the 3-bettor (out of position) and the button the caller.
- `ranges/srp_spr1.5.json` (about 11bb): button open 2.2bb, big blind flat call
- `ranges/srp_spr3.5.json` (about 23bb)
- `ranges/srp_spr6.json` (about 38bb)
- `ranges/3bp_spr1.5.json` (about 30bb): big blind 3-bet to 7bb, button call
- `ranges/3bp_spr3.5.json` (about 60bb)
- `ranges/3bp_spr6.json` (about 97bb)
Flat-call ranges leave out the hands that would 3-bet or 4-bet. Live combos on Ks7d2c: SRP
about 500-700 per player, 3-bet pots about 140-180.

### Compute-saving choices for the library (recommendations to Part B2)
- One ICM solve serves two library entries: cell (a,b) with hero in position and cell (b,a)
  with hero out of position use the same two players in the same seats (all 12 off-diagonal
  cells have mirrored pairs in `config/buckets.json`). With 14 cells that is 16 ICM solves per
  (pot type, SPR, flop) instead of 28, plus 1 chip-EV solve instead of 2 (one chip-EV solve gives
  both players' strategies).
- `ss_warm_start` (regrets of a finished solve, fresh average) cut ICM iterations by 21-52% on
  SPR 3.5 and 6 flops (SRP SPR 6: 290 -> 140); at SPR 1.5 it made no consistent difference.
  See PROGRESS.md for numbers.
- `compact_storage = 2` with `memory_limit` keeps each solve near the 3 GB budget only up to
  SPR 3.5 single-raised pots; SPR 6 single-raised pots need about 3.6 GB even with 16-bit storage
  (7 GB with float32), so run at most 3 of those at once.

### DCFR parameters (tested, defaults kept at alpha 1.5, beta 0, gamma 2)
Iterations to 0.5% of the pot, single solves:
- Turn+river SRP benchmark (chips): (1.5,0,2) 140; (1.5,0.5,2) 140; (2,0,2) 160; (1.5,0,3) 130;
  (1,0,1) 430; (3,0,2) 190.
- Flop 3-bet SPR 3.5, ICM: (1.5,0,2) 180; (1.5,0.5,2) 150; (1.5,0,3) 180; (2,0.5,2) 190;
  (1.5,1,2) did not converge in 1000.
- beta 0.5 on other flops: SRP SPR 1.5 ICM 80 (vs 60), 3-bet SPR 1.5 ICM 80 (vs 60), 3-bet SPR 3.5
  chips 130 (vs 110). No setting is better across the board, so the defaults stay.
- Exploitability is not always monotonic with beta 0 (a temporary rise of up to ~40% was seen
  twice, e.g. SRP SPR 6 at iteration 200); the solves still converge.
- Warm-start source (3-bet SPR 3.5, Ks7d2c, a second matchup on the runaway table, iterations to
  0.5%): cold 170; warm from the chip-EV solve 120; warm from another matchup's ICM solve 150.
  So every ICM solve should warm-start from the chip-EV solve of its spot, not from a neighbour.

## B2 (library tools and starter library), 2026-10-28

### Spots, solves and the manifest (`manifest/solves.tsv`, `manifest/entries.tsv`)
- A **library entry** is what the app looks up: (hero bucket, villain bucket, position, pot type,
  SPR, flop). A **solve** is one solver run. `entries.tsv` maps every entry to its ICM solve, which
  player is the hero inside it (0 = OOP / big blind, 1 = IP / button), and its chip-EV solve.
  `results/<id>.json.gz` and `raw/<id>.bin` are keyed by solve id, so the spec's
  `results/<spotId>.json.gz` uses the solve id as spot id.
- Cell (a,b) with hero in position and cell (b,a) with hero out of position are the same two
  players in the same seats, so they share one solve (B1 decision). Per pot type, SPR and flop:
  1 chip-EV solve + 16 ICM solves for the 28 ICM entries. Totals: T1-T3 204 solves each,
  T4 816, T5 408; 1,836 solves for 3,024 entries.
- Solve ids: `<srp|3bp>-spr<15|35|60>-<flop>-chip` and `...-<rcl|cl>-o<OOP seat>i<IP seat>`
  (rcl = runaway chip leader table, cl = chip leader table; seats index `stacks_bb` in
  `config/buckets.json`, 0 = largest).
- Tier of a spot: flop 1 at SPR 1.5 / 3.5 / 6 = T1 / T2 / T3; flops 2-3 at SPR 1.5 or 3.5 = T4,
  at SPR 6 = T5.

### ICM table of a spot
- Pot 6bb (single-raised) or 15bb (3-bet); effective stack E = SPR x pot.
- The whole canonical table is scaled by one factor k so that the shorter player of the pair has
  exactly E behind after putting in half the pot: k = (E + pot/2) / min(stack of the two players).
  Every stack becomes k x canonical; the two players then have pot/2 less behind. The pot is
  treated as coming from the two players (blinds and antes of the others are ignored).
- Why: ICM equity and the risk premium depend only on stack proportions, so scaling keeps each
  matchup's ICM pressure (and its bucket) exactly, while the spec's "other stacks keep their
  canonical values" can't hold literally (a 10bb player can't have 36bb behind at SPR 6). The
  bigger player keeps their extra chips (behind > E); the solver's effective stack is E.
- Payouts 40/25/15/10/6/4, all six players.

### Summaries (`tools/lb_summary.c`) and reader (`libreader.c`)
- Definitions are in `LIBRARY_FORMAT.md` ("How the values are computed"). Main choices: bet % is
  measured at hero's first chance to bet on the flop using hero's preflop range weights; bet
  size is the size with the most betting mass; check-raise % averages over the in-position bet
  sizes that can be raised, weighted by how often each is bet; the stack-off threshold pools all
  flop nodes where hero faces an all-in, weighted by the probability of reaching them; bluff % is
  the weak-draw + air share of the betting mass.
- Each result stores both players' summaries, so one solve fills its two library entries.
- Chip-EV values of an entry come from the chip-EV solve of the same spot, for the same seat
  (position).
- Reader: bilinear over premium bucket centres, linear over SPR, average over the solved flops of
  the class; categorical values from the heaviest corner; nearest-spot fallback otherwise.
  Premiums outside the outer bucket centres are clamped.

### Tests 5 and 6
- Test 6 is `librarybuilder test6` on the real `library/LiteLibrary.bin`: every value of every
  packed entry is read back with `libreader` and compared with the value in the result file
  (relative tolerance 2^-11, the binary16 rounding bound). The unit test `test_library` checks the
  same round trip plus queries on synthetic data.
- Test 5 is `librarybuilder test5 $(cat manifest/pilot.txt)`: the 20 pilot solves (10 per pot type,
  all three SPRs, five flop classes, chip EV and ICM) must all reach 0.5% for both players below
  the 1000-iteration cap. The spec asks for 5 per pot type; the pilot gives 10.

### Builder memory budget
- The session's memory cgroup kills processes at about 14 GB resident. The budget counts each
  solver's own allocations (`ss_memory_required`) plus held chip-EV solvers, but not malloc
  overhead, thread stacks or summary buffers, so it must stay well below the limit: use
  `--budget-mb 10500` (13.5 GB was killed at 12:01 in B2).

## Build session 3

- Warm starts load the chip-EV state file directly into each ICM solver instead of copying from
  a chip-EV solver held in memory (same result, no extra memory). State files are removed once
  every dependent of the run has loaded them; leftover files after a stop are reused next run.
- Memory budget 11 GB (no held solvers any more; B2 measured resident memory about 0.5 GB under
  the budget). No new solve after 10:00 UTC so that a 2-hour single-raised SPR 6 solve still ends
  before the 11:21 wrap-up.
- `tools/verify_results.py` is the task-1 check of `results/` against the manifest.
- Mid-solve checkpoints every 15 minutes (`work/ckpt/`, not committed) because the container can
  restart without warning (it did at 23:50 in session 3) and single-raised SPR 6 solves take about
  2 hours. Saving a 3.6 GB state takes a few seconds, well under 1% of solve time.
