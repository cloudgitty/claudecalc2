# Part B progress (ICMKit)

Session B1 (C engine) start: 2026-10-28 00:17:39 UTC (`date -u`). Wrap-up deadline: 13:17 UTC.

Note: the repo-root `PROGRESS.md` belongs to the unrelated PLO6 project and is left untouched;
Part B progress lives here in `ICMKit/PROGRESS.md`. Decisions are in the repo-root `DECISIONS.md`.

History: an earlier attempt (00:05 UTC) to build the engine in Swift stopped because no Swift
toolchain could be installed. The prompt was revised to a portable C11 core; that is this session.

## Log

- 00:18 Step 0: new spec saved unchanged as `docs/PART_B_SPEC.md`.
- 00:27 Task 1 done: package layout (`ICMKit/Package.swift`, `Sources/ICMCore` C target with
  `include/icmcore.h` + `module.modulemap`, `Sources/ICMKit` Swift layer), root `Makefile`
  (`make`, `make test`, `make test-full`, `make asan`).
- 00:27 Task 2 done: `icm.c`; test 2 passes (1,240 random tables of 2-9 players vs brute force over
  all finishing orders, worst difference 9e-12; equities sum to the pool).
- 00:27 Task 3 done: `handeval.c`; test 1 passes in full: all 133,784,560 seven-card hands match the
  standard category counts (5.2 s). Fast subset kept in `make test`.
- 00:27 Task 4 done: `flopclass.c`; all 18 representative flops land in their class, list unchanged.
- 00:28 Task 5 done: `riskpremium.c`, `tools/buckets.c` -> `config/buckets.json`. Thresholds
  3.59% / 6.98% / 10.11%; 14 cells, skipped (0,0) and (2,2). Reasoning in `DECISIONS.md`.
- 00:49 Task 6: `streetsolver.c` + `threadpool.c`. Test 3 (toy river) passes for s = 0.33, 0.75,
  1.5 (all within 0.1 points). Test 4 passes: winner-take-all ICM and chip EV
  give equal values, and the ICM strategy played in the chip game is as unexploitable (0.27% /
  0.28%) as the chip solution, while the real-payout ICM strategy is 9% exploitable there. `test_solver` checks tree rules, zero-sum
  chip values, isomorphism, thread determinism, cancel and the memory limit.
- 00:51 All tests (`make asan`) pass under AddressSanitizer + UndefinedBehaviorSanitizer
  (LeakSanitizer on): no reports.
- 00:55 Task 7: Swift layer written, **not yet compiled** (no Swift toolchain here):
  `ICMKit/Sources/ICMKit/Cards.swift`, `CoreICM.swift`, `StreetSolver.swift`, `PostflopLibrary.swift`.
  See "Swift layer: first Xcode build checklist" below.
- 01:00-02:45 Speed work (one-walk best response, faster showdown sweep), optional 16-bit
  storage, warm start, hand categories. Re-ran `make asan` at 02:45: all pass.
- 01:07-06:15 Early estimate runs (task 9) and DCFR parameter tests; benchmarks (task 8).
- 06:25 State save/load (`ss_save_state` / `ss_load_state` / `ss_restart_average`) so a chip-EV
  solve can seed ICM solves from disk; final `make asan` run (see end of file).

## Swift layer: first Xcode build checklist (files not yet compiled)

Files: `ICMKit/Package.swift`, `ICMKit/Sources/ICMKit/{Cards,CoreICM,StreetSolver,PostflopLibrary}.swift`.
When the package is first opened in Xcode (iOS 18 target):
1. `Package.swift`: tools version 6.0 parses; `.unsafeFlags(["-O3"])` may be rejected when the
   package is used as a dependency - delete that line if so (Release builds already optimise).
2. The C target imports as module `ICMCore` through `include/module.modulemap` (umbrella
   `icmcore.h`). Check that `import ICMCore` works and that SwiftPM does not also generate its
   own module map (if it complains about two module maps, delete `module.modulemap`; SwiftPM's
   generated umbrella-directory map exposes the same headers).
3. C fixed-size arrays import as tuples: `ss_config.board` (5 x UInt8), `icm_stacks`,
   `icm_payouts` (9 x Double), `seat` and `range` (2-tuples). `StreetSolver.init` writes them
   with `withUnsafeMutableBytes`; check the tuple layouts and that `c.range = (o.baseAddress,
   i.baseAddress)` type-checks (`UnsafePointer<Float>?` pairs).
4. The progress callback is a `@convention(c)` closure literal assigned to `c.progress`; it must
   not capture context (it only uses the `user` pointer). Check it compiles under Swift 6 strict
   concurrency; mark it `@Sendable` or move it to a global function if needed.
5. `StreetSolver` and `PostflopLibrary` are `@unchecked Sendable` classes: `cancel()` and
   `iterationsDone` are thread-safe in C; everything else must stay on one thread.
6. Enum constants from C (`SS_MODE_ICM`, `SS_CANCELLED`, `NUM_COMBOS`, `SS_MAX_PLAYERS`) import as
   `Int32`/`Int` values or as members of an anonymous enum type; adjust the `Int32(...)`
   conversions if the compiler imports them differently.
7. `card_format`'s buffer is `[CChar]` of 3; `String(cString:)` on it.
8. `PostflopLibrary` wraps `lr_open_memory`, which fails until Part B2 implements the reader.
9. Parity check (spec "Checks for later on the Mac"): compare `CoreICM.equity` with the app's
   Swift `ICMEngine` on the same tables (for example the two canonical tables and random tables).
10. Device timings: run a river and a turn solve on an iPhone 13 (see Benchmarks).

## Benchmarks (task 8): on-device spots vs the iPhone 13 targets

Machine: cloud Intel Xeon vCPU @ 2.8 GHz, gcc 13 `-O3` for baseline x86-64 (SSE2, similar vector
width to ARM NEON; no `-march=native`). An iPhone 13 (A15: 2 performance + 4 efficiency cores)
is ARM, so these numbers are a guide only; the real check is on a device.
Tool: `build/bench <ranges.json> [river|turn|all]` (env `ICM=1` for ICM payoffs, `ONE_THREAD=1`).
Spec tree (33% / 75% / all-in, raise 3x, one raise), stop at 0.5% of the pot, cap 1000 iterations.
Ranges are the full placeholder preflop ranges (a worst case: on device the ranges reaching the
turn or river are narrower after earlier action).

Final run on the idle machine (06:20 UTC; timings vary by about 10% between runs on this vCPU):

| Spot | Ranges (live combos OOP/IP) | Mode | 1 thread | 4 threads | Iterations |
| --- | --- | --- | --- | --- | --- |
| River Ks7d2c4h9s, pot 20, stack 60 | SRP 38bb (599/535) | chip EV | 0.030 s | 0.026 s | 50 |
| River, same | SRP 38bb | ICM | 0.029 s | 0.030 s | 70 |
| River, same | 3-bet 97bb (140/158) | chip EV | 0.013 s | 0.009 s | 80 |
| River, same | 3-bet 97bb | ICM | 0.008 s | 0.008 s | 80 |
| Turn+river Ks7d2c4h, pot 10, stack 50 | SRP 38bb (630/563) | chip EV | 14.3 s | 5.8 s | 140 |
| Turn+river, same | SRP 38bb | ICM | 13.4 s | 4.7 s | 130 |
| Turn+river, same | 3-bet 97bb (145/167) | chip EV | 3.6 s | 1.6 s | 140 |
| Turn+river, same | 3-bet 97bb | ICM | 5.0 s | 2.3 s | 190 |

Against the targets: river under 2 s is met with a margin of about 60x; turn plus river under
20 s is met single-threaded even with full-width SRP ranges (13-14 s) and with 4 threads in
5-6 s. River solves gain nothing from threads (no chance node to split). Memory: river < 1 MB, turn 10-40 MB (16-bit storage halves it if ever needed).
Turn trees have 8,253 nodes (48 river cards, no suit symmetry on this board); boards with suit
symmetry are smaller.

## Early estimate (task 9): flop solve cost and library compute

Setup: flop Ks7d2c (rainbow: no suit symmetry, the most expensive kind), placeholder ranges from
`ranges/`, spec tree, stop at 0.5% of the pot (both players), cap 1000. Every solve ran
**single-threaded with 4 solves running at once on the 4 cores**, i.e. as the library build
would run. ICM table: the chip-leader table with the 50bb and 35bb players replaced by the two
players of the spot, each with the effective stack behind (others 60/30/15/10bb, payouts
40/25/15/10/6/4). Memory is the solver's own count; above 4 GB the solver switched to 16-bit
storage (SRP SPR 6 only). Raw output: `docs/estimate/estimate_raw.txt`.
Command: `MODE=chips|icm|warm build/estimate 1 Ks7d2c <spot>`.

| Spot | Pot / stack | Live combos | Tree nodes | Memory | Chip EV | ICM, cold | ICM, warm from chip EV |
| --- | --- | --- | --- | --- | --- | --- | --- |
| SRP SPR 1.5 | 6 / 9 | 519/478 | 136k | 490 MB | 50 it, 80 s | 60 it, 89 s | 70 it, 107 s |
| SRP SPR 3.5 | 6 / 21 | 552/521 | 563k | 2.18 GB | 150 it, 957 s | 140 it, 880 s | 110 it, 663 s |
| SRP SPR 6 | 6 / 36 | 644/576 | 1.59M | 3.57 GB (16-bit; 7.0 GB float32) | 310 it, 7,233 s | 290 it, 6,462 s | 140 it, 3,329 s |
| 3-bet SPR 1.5 | 15 / 22.5 | 157/177 | 136k | 170 MB | 50 it, 27 s | 60 it, 31 s | 50 it, 25 s |
| 3-bet SPR 3.5 | 15 / 52.5 | 140/157 | 563k | 628 MB | 110 it, 198 s | 180 it, 309 s | 140 it, 234 s |
| 3-bet SPR 6 | 15 / 90 | 146/171 | 1.59M | 1.88 GB | 150 it, 757 s | 300 it, 1,454 s | 170 it, 802 s |

All solves reached the 0.5% target (final values in the raw file); none hit the cap. The four
spots the task asked for are the SPR 1.5 and SPR 6 rows; SPR 3.5 was added to project T2/T4.
Peak memory equals the solver's count (RSS within 1%). A 4-thread run of 3-bet SPR 6 took 232 s
(vs 749 s single-threaded, 3.2x).

**Other flops.** Suit symmetry shrinks trees: paired and two-tone flops are about 0.6x the
rainbow tree, monotone about 0.28x (measured on all 18 representative flops). Iterations also
vary by flop: cold ICM, 3-bet SPR 3.5, flop 1 of each class took 309 / 473 / 229 / 127 / 90 /
266 s (Ks7d2c, 9s5d2c, KsKd4c, AsJs5d, Ks9s4s, 9s8d7c): mean 0.81x Ks7d2c. The projection
uses 0.81 for every flop.

**Projection** (`python3 tools/project_estimate.py docs/estimate/solve_seconds.json`). Solve
counts with the 14 matchup cells that exist (`config/buckets.json`):
- *Spec as written:* per (pot type, SPR, flop) 28 ICM + 2 chip-EV solves, all cold.
- *Deduplicated:* one ICM solve serves cell (a,b) in position and cell (b,a) out of position
  (mirrored pairs), so 16 ICM solves; one chip-EV solve serves both positions.
- *Deduplicated + warm:* each ICM solve warm-starts from the chip-EV solve of the same spot
  (only where that helped: SPR 3.5 and 6; at SPR 1.5 the faster of cold and warm is used).

| Tier | Spec as written (core-h) | Deduplicated (core-h) | Dedup + warm (core-h) | Dedup + warm, wall h on 4 cores |
| --- | --- | --- | --- | --- |
| T1 (SPR 1.5, flop 1) | 4.8 | 2.7 | 2.6 | 0.7 |
| T2 (SPR 3.5, flop 1) | 48 | 27 | 21 | 5.2 |
| T3 (SPR 6, flop 1) | 321 | 182 | 100 | 25 |
| T4 (SPR 1.5 + 3.5, flops 2-3) | 106 | 60 | 47 | 12 |
| T5 (SPR 6, flops 2-3) | 642 | 364 | 200 | 50 |
| **Total** | **1,121** | **635** | **371** | **93** |

**Sessions needed** (14-hour sessions with about 12 hours of solving on 4 cores = 48 core-hours):
- spec as written: about 23 sessions;
- deduplicated: about 13 sessions;
- **deduplicated + warm start: about 8 sessions**, plus about one more because SRP SPR 6 solves
  (3.6 GB each; warm start from a saved state file keeps one solver per process) can only run 3
  at a time within 15 GB. So about **9 sessions** for T1-T5.
- T1 and T2 together need only ~6 hours, so B2 can finish them in its own session while it
  writes the builder; T3 needs about 2.5 sessions, T4 one, T5 about 5.
- SRP SPR 6 is about 70% of all compute (2 h per chip-EV solve, 0.9 h per warm ICM solve).
  Ways to cut it, if the time is not available (each changes the spec, so it is a product
  decision): narrower SRP ranges (cost scales with combos per player), one river bet size at
  SPR 6, or a 1% stop target for SPR 6 (going from 1% to 0.5% takes about half of the iterations).

## Summary of session B1

### Finished
- Step 0 and tasks 1-9. Tests 1-4 pass, also under ASan + UBSan (`make asan`); test 1 also in full
  (`make test-full`).
- `config/buckets.json` (14 cells, 2 skipped), the Swift layer files (not compiled), the
  benchmarks and the early estimate (above).
- Extras that Part B2 will need: `range.c` (range notation), `handcat.c` (the spec's six hand
  categories on the flop), `ss_warm_start`, 16-bit storage option, `ss_copy_strategy` /
  `ss_evaluate`, placeholder ranges in `ranges/`, the `libreader.h` interface.

### Not finished / not in scope of B1
- Swift layer not compiled (no Swift on Linux here): see the checklist above.
- `libreader.c` is a stub returning "not implemented"; the library format, the builder
  (`tools/librarybuilder`), packing, `LiteLibrary.bin`/`.json`, `LIBRARY_FORMAT.md` and spec
  tests 5 and 6 are Part B2's work.
- Mac-only checks: Xcode build, parity of C `icm` with the app's Swift `ICMEngine`, device timings.

### What Part B2 must know
1. **Build:** `make` at the repo root; `make test` (fast, ~1 min), `make asan`. Engine headers:
   `ICMKit/Sources/ICMCore/include/icmcore.h`. `ICMKit/README.md` has a solve example.
   The repo root also holds an unrelated PLO6 project (`src/`, `app/`, root `PROGRESS.md`): leave
   it alone; Part B progress lives in this file.
2. **Solver API** (`streetsolver.h`): `ss_config_default`, `ss_create`, `ss_solve`, tree walking
   with `ss_root` / `ss_node_*` / `ss_chance_child`, average strategy per node with
   `ss_node_strategy` (`out[action * 1326 + combo]`). Player 0 = out of position (big blind),
   1 = in position (button). Amounts are "to" amounts on the street. Values in `ss_result` are
   relative to giving up the starting pot (chips, or $ in ICM mode).
3. **ICM setup is B2's call:** `icm_stacks` are every player's chips *behind* at the flop, the pot
   goes to the winner, other stacks are fixed. How the canonical stacks and the SPR's effective
   stack combine (the spec only says effective stacks are set from the pot) must be decided and
   written in `DECISIONS.md`. The estimate gave both players exactly the effective stack behind.
4. **Compute plan:** see the early estimate. Use the mirrored-cell deduplication (16 ICM + 1
   chip-EV solve per pot type, SPR and flop) and warm-start each ICM solve from the chip-EV
   solve (`ss_warm_start(icm, chips, 1.0)`) for SPR 3.5 and 6. Run solves single-threaded, 4 at
   a time (3 for SRP SPR 6), with `compact_storage = 2` and `memory_limit` about 4 GB.
   Expect about 9 sessions for T1-T5 at the spec's settings. Save the chip-EV solve with
   `ss_save_state` (SRP SPR 6: 3.4 GB file in 16-bit storage) and start each ICM solve with
   `ss_load_state` + `ss_restart_average(s, 1.0)`; then only one solver per core is in memory.
5. **Flop summaries:** `hand_category()` gives the spec's categories (definitions in
   `DECISIONS.md`). For "bet %" use the hero's first decision node on the flop (IP: the node after
   the OOP check). "Stack-off threshold" needs the flop nodes where hero faces an all-in.
6. **Exploitability** is per player, in % of that player's pot value; the stop rule needs both
   under 0.5%. Exploitability can rise briefly before converging (DCFR); judge by the final check.
7. Placeholder ranges are wide (SRP 500-700 live combos per player); cost scales roughly with
   combos, so real solver ranges (if narrower) make the library cheaper.

### Final check (06:22 UTC)
`make test-full` and `make asan` both pass on the final code (all 7 test programs; the full
133,784,560-hand enumeration included in `test-full`).

---

# Part B2: library tools and starter library

Session B2 start: 2026-10-28 08:44:43 UTC (`date -u`). Wrap-up at 21:44 UTC.

## Log
- 08:45 Task 1 (catch up): clean build of core, tools and tests; `make test` passes (7 programs).
  B1 left only the library reader (B2 task 6) and the Swift compile (needs a Mac).
- 08:45 Task 2 (ranges): the repo has no preflop solver, so the six placeholder files from B1 in
  `ranges/` are used as they are (`"placeholder": true`, listed in `DECISIONS.md`).
- 08:53 Task 3 (manifest): `librarybuilder manifest` -> `manifest/solves.tsv` (1,836 solves) and
  `manifest/entries.tsv` (3,024 library entries): T1-T3 204 solves / 336 entries each, T4 816 /
  1,344, T5 408 / 672. Mapping and ICM table choices in `DECISIONS.md`.
- 08:55 Task 4 (builder): `tools/librarybuilder` (`librarybuilder.c`, `lb_common.c`,
  `lb_summary.c`, `lb_pack.c`): threaded runner (4 single-threaded solves, memory budget,
  `--stop-at`, `--first`), resumable (skips `results/<id>.json.gz` that exist), atomic gzip
  results, raw flop strategies in `raw/`, log in `logs/build.log`. ICM solves at SPR 3.5 / 6
  warm-start from their chip-EV solve held in memory.
- 08:57 Task 5 (summaries) and task 6 (packer, reader): `lb_summary.c` (five numbers + hand
  categories per player), `lb_pack.c` (`library/LiteLibrary.bin` + `.json`), `libreader.c`
  (interpolation + nearest-spot fallback), `LIBRARY_FORMAT.md`, Swift `PostflopLibrary.swift`
  (**not yet compiled**). New unit test `test_library` (synthetic results: round trip,
  exact lookup, bucket and SPR interpolation, fallback, CRC) passes.
- 09:06 Sanitizers: `make asan` (8 test programs incl. `test_library`) passes; the builder ran 2
  real solves and a warm-start chain (chip EV + 2 ICM) under ASan + UBSan with no reports.
- 09:13 Task 8 + 9 started as one run: `librarybuilder run --jobs 4 --tiers 1,2 --first <pilot>
  --stop-at 21:20 UTC`. The 20 pilot solves (`manifest/pilot.txt`, 4 per tier, both pot types,
  chip EV + ICM pairs) go first, then T1, then T2. Results are committed about every 45 minutes.
- 10:19 Scheduling fix: the runner reserved worst-case memory (B1 rainbow sizes, e.g. 3.7 GB
  for SRP SPR 6) for every running solve and for each chip-EV solver held for warm starts; with
  four held solvers only one worker could run from about 10:09. Stopped the runner (losing the
  65-minute SRP SPR 6 rainbow chip-EV solve in progress) and restarted with a binary that
  reserves each solver's real size once it exists (`ss_memory_required`) and a 13.5 GB budget.
  Consequence: the ICM solves whose chip-EV solve was already finished (3 pilot groups and the
  T2 3-bet Ks9s4s group) run cold instead of warm, so those pilot timings are pessimistic.
