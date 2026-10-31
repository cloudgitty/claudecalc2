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
- 11:37 **T1 complete** (204 / 204 solves, 336 entries, 3.4 core-hours). Packed:
  `library/LiteLibrary.bin` 349 entries, 9,836 bytes, T1 flagged complete; test 6 passes
  (3,490 values, 0 mismatches).
- 12:01 The runner was killed by the out-of-memory killer (14.0 GB resident: the session's
  memory cgroup limit is about 14 GB, not the 15 GB `free` shows). In-flight solves were lost
  (at most ~10 minutes each) and the held SRP SPR 6 rainbow chip-EV solver too, so that pilot
  ICM solve runs cold. Restarted at 12:10 with a 10.5 GB budget (`--budget-mb 10500`).

## Test 5 and the pilot (tasks 7 and 8)

**Test 5 passes** (14:05): `librarybuilder test5 $(cat manifest/pilot.txt)`: all 20 pilot solves
(10 single-raised, 10 3-bet; SPR 1.5, 3.5 and 6; dry high, paired, two-tone, monotone, connected
and dry low flops; chip EV and ICM) reached 0.5% of the pot for both players, in 50-300
iterations (cap 1000).

**Pilot** (`python3 tools/pilot_report.py`): 20 solves, 4 per tier, run single-threaded with 4
solves at a time. They count toward the build. Caveats: the 3 SPR 6 ICM pilots and the 3-bet
SPR 3.5 paired ICM pilot ran cold because of the two runner restarts (warm start would have
been ~25-50% faster); timings include memory-bandwidth contention from the other 3 solves.

| Solve | Tier | Mode | Iterations | Exploitability % (OOP / IP) | Seconds | Memory MB | Warm |
| --- | --- | --- | --- | --- | --- | --- | --- |
| srp-spr60-8h4d3c-chip | T5 | chip | 220 | 0.492 / 0.284 | 6013 | 3649 | no |
| srp-spr60-Ks9s4s-chip | T3 | chip | 260 | 0.474 / 0.351 | 1729 | 2016 | no |
| 3bp-spr60-AsJs5d-chip | T3 | chip | 210 | 0.498 / 0.304 | 720 | 1079 | no |
| 3bp-spr60-Jh7h3h-chip | T5 | chip | 300 | 0.476 / 0.177 | 542 | 544 | no |
| srp-spr60-8h4d3c-cl-o1i2 | T5 | icm | 260 | 0.499 / 0.421 | 7004 | 3649 | no |
| srp-spr60-Ks9s4s-rcl-o0i3 | T3 | icm | 230 | 0.489 / 0.282 | 1575 | 2016 | no |
| 3bp-spr60-AsJs5d-cl-o2i1 | T3 | icm | 230 | 0.408 / 0.473 | 841 | 1079 | no |
| 3bp-spr60-Jh7h3h-rcl-o3i0 | T5 | icm | 220 | 0.486 / 0.388 | 428 | 544 | no |
| srp-spr35-9s8d7c-chip | T2 | chip | 120 | 0.440 / 0.325 | 878 | 2132 | no |
| srp-spr35-9s8d7c-cl-o1i2 | T2 | icm | 90 | 0.486 / 0.444 | 637 | 2132 | yes |
| 3bp-spr35-Ks9s4s-chip | T2 | chip | 140 | 0.480 / 0.244 | 92 | 178 | no |
| 3bp-spr35-Ks9s4s-rcl-o5i1 | T2 | icm | 170 | 0.492 / 0.293 | 106 | 178 | yes |
| 3bp-spr35-8s8d3h-chip | T4 | chip | 120 | 0.491 / 0.251 | 163 | 407 | no |
| 3bp-spr35-8s8d3h-cl-o0i1 | T4 | icm | 130 | 0.440 / 0.285 | 180 | 407 | no |
| srp-spr15-Qh9h4c-chip | T4 | chip | 50 | 0.409 / 0.342 | 56 | 299 | no |
| srp-spr15-Qh9h4c-rcl-o3i4 | T4 | icm | 60 | 0.350 / 0.279 | 64 | 299 | no |
| srp-spr15-Ks7d2c-chip | T1 | chip | 50 | 0.458 / 0.486 | 87 | 490 | no |
| srp-spr15-Ks7d2c-cl-o2i1 | T1 | icm | 80 | 0.414 / 0.373 | 136 | 490 | no |
| 3bp-spr15-KsKd4c-chip | T1 | chip | 50 | 0.473 / 0.310 | 19 | 101 | no |
| 3bp-spr15-KsKd4c-rcl-o4i3 | T1 | icm | 60 | 0.326 / 0.217 | 23 | 101 | no |


| Tier | Pilot solves | Median s | Slowest s | Peak memory MB | Projected tier core-h | Solves done / total | Core-h left | Sessions left |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| T1 | 4 | 55 | 136 | 490 | 4.4 | 204 / 204 | 0.0 | 0.0 |
| T2 | 4 | 371 | 878 | 2132 | 21.4 | 30 / 204 | 18.3 | 0.4 |
| T3 | 4 | 1208 | 1729 | 2016 | 68.5 | 4 / 204 | 67.2 | 1.4 |
| T4 | 4 | 113 | 180 | 407 | 27.5 | 4 / 816 | 27.3 | 0.6 |
| T5 | 4 | 3278 | 7004 | 3649 | 418.2 | 4 / 408 | 414.1 | 8.6 |

The per-tier projection multiplies each tier's pilot chip-EV and ICM times (per pot type) by its
solve counts. With one or two flops per pot type it depends heavily on the flops: T3's pilot
flops are monotone and two-tone (cheap trees) so T3 is underestimated, T5's single-raised pilot is
a rainbow flop with a cold ICM solve so T5 is overestimated. See the wrap-up for a projection that
also uses every solve of the build.
- 14:15 Builder: chip-EV solvers kept for warm starts are now also saved to `work/state/`
  (git-ignored; 50 MB to 3.5 GB each) and reloaded after a restart or crash; the file is removed
  once the last dependent ICM solve has copied it. Tested (normal + ASan/UBSan): a solve started in
  a second run warm-starts from the saved state. The B2 run keeps its binary; the new one
  (`build-next/librarybuilder`, also in the source) is for the next run. In B2 the 12:01 crash
  had lost 4 held T2 chip-EV solvers, so ~60 T2 ICM solves ran cold (~40% slower).
- 18:48 **T2 complete** (204 / 204 solves, 336 entries, 25.7 core-hours); the first runner
  (tiers 1-2) exited normally.
- 18:48-20:05 Cores idle: my wait loop's process check matched its own command line
  (`pgrep -f` on a string that was in the loop itself), so it did not notice the runner had
  exited. About 75 minutes of solving lost.
- 20:06 T3 started with the new builder binary (states saved to disk), **3-bet spots only**: a
  single-raised SPR 6 chip-EV solve takes ~2 hours and would run past the wrap-up. No new solve
  after 21:20.
- 21:20 No new solves after this time; the last running T3 solves finished at 22:00 (runner exit 0).
- 22:01 Wrap-up: packed `library/LiteLibrary.bin` (699 entries, 19,636 bytes, T1 + T2 flagged
  complete) and `library/LiteLibrary.json`; **test 6 passes** (6,990 values, 0 mismatches).

## Summary of session B2

### Spots solved per tier (solves / library entries)

| Tier | Solves done / total | Entries done / total | Solves left | Compute spent (core-h) | Median solve (s) | Slowest (s) |
| --- | --- | --- | --- | --- | --- | --- |
| T1 | 204 / 204 | 336 / 336 | 0 | 3.4 | 43 | 324 |
| T2 | 204 / 204 | 336 / 336 | 0 | 25.7 | 285 | 1,493 |
| T3 | 15 / 204 | 20 / 336 | 189 | 5.8 | 1,409 | 2,799 |
| T4 | 4 / 816 | 3 / 1,344 | 812 | 0.1 | 163 | 180 |
| T5 | 4 / 408 | 4 / 672 | 404 | 3.9 | 6,013 | 7,004 |
| **Total** | **431 / 1,836** | **699 / 3,024** | **1,405** | **38.9** | | |

(T3 and T4 counts include pilot solves; T3 also the 3-bet Ks7d2c and other 3-bet groups started
in the last hour.)

- **Total compute so far:** 38.9 core-hours of solving (plus ~0.1 in B1's tests).
- **Median exploitability:** 0.454% of the pot (worse of the two players) over 431 solves; every
  solve met the 0.5% target, none hit the 1000-iteration cap. Peak solver memory 3.6 GB.
- **Library file:** `library/LiteLibrary.bin` 19,636 bytes for 699 entries (64 + 28 bytes per
  entry; the full 3,024-entry library will be 84,736 bytes, far under 1 MB).
- **Tests:** 5 and 6 pass; all 8 unit-test programs pass, also under ASan + UBSan.

### Estimated sessions still needed

`python3 tools/project_remaining.py`: measured seconds per MB of solver memory per pot type, SPR
and mode, from all 431 solves (real build conditions, 4 at a time), applied to every unsolved
solve with its flop's tree size.

| Tier | Core-h left | Sessions (48 core-h each) |
| --- | --- | --- |
| T3 | 149 | 3.1 |
| T4 | 46 | 1.0 |
| T5 | 313 | 6.5 |
| **Total** | **508** | **10.6** |

Single-raised SPR 6 has only cold ICM measurements (the pilot's warm sources were lost in the
restarts), so T3/T5 are probably ~20-30% pessimistic: with warm starts at SPR 6 (B1: 290 -> 140
iterations) expect about **8-10 more sessions**, of which T5's single-raised spots are most.
Single-raised SPR 6 solves (3.6 GB each) limit concurrency to 2-3 under the 10.5 GB budget.

### What Part B3 must know

1. **Run:** `make` then, for example,
   `./build/librarybuilder run --jobs 4 --tiers 3,4,5 --budget-mb 10500 --stop-at <epoch>`.
   It skips solved spots (`results/*.json.gz`), so it resumes where B2 stopped. Commit `results/`,
   `raw/`, `logs/`, `library/` about hourly; `pack` + `test6` at the end;
   `python3 tools/project_remaining.py` for the projection; `librarybuilder status` for counts.
2. **Memory:** the container is killed at about 14 GB resident (B2 lost a run at 12:01 with a
   13.5 GB budget). Keep `--budget-mb 10500`.
3. **Warm starts:** the builder now saves chip-EV states to `work/state/` (not committed) so ICM
   solves stay warm after a restart. B2 left three 3-bet SPR 6 states there (5 GB), but the
   container is reclaimed after the session, so B3 starts those groups' ICM solves cold.
4. **Single-raised SPR 6 groups:** the chip-EV solve takes ~2 hours and holds 3.6 GB while its 16
   ICM solves run. Start them early in a session; never start one in the last ~3 hours.
5. **Watching the runner:** check it with `pgrep -f "^./build/librarybuilder run"` (anchored).
   An unanchored `pgrep -f` matches the shell running the check, which cost B2 75 minutes.
6. **Not done here:** Swift layer (incl. `PostflopLibrary.swift`) still not compiled; Mac checks
   (Xcode build, ICM parity with the app's `ICMEngine`, device timings) remain.
7. Placeholder ranges are still in use; real preflop ranges would change every result.

---

# Build session 3

Start: 2026-10-28 22:21:34 UTC (`date -u`). Wrap-up at 11:21 UTC; no new solves after 10:00 UTC
(a single-raised SPR 6 solve can take ~2 hours).

## Log
- 22:25 Task 1 (check): core, tools and tests build; `make test` passes (8 programs);
  `python3 tools/verify_results.py`: all 431 results match the manifest (ids, tier, pot type,
  SPR, flop, mode, seats, both summaries, exploitability <= 0.5%), no stray raw files.
- 22:28 Builder change: chip-EV solvers are no longer held in memory for warm starts. The
  chip-EV state is saved to `work/state/`, and each dependent ICM solve loads it straight into
  its own solver (`ss_load_state` + `ss_restart_average`). Identical results to the in-memory
  path (same iterations); tested normally and under ASan + UBSan. Frees up to 3.6 GB per
  single-raised SPR 6 group. Chip-EV solves with dependents are not started while less than
  10 GB of disk is free.
- 22:29 Task 2 (solve): `librarybuilder run --jobs 4 --tiers 3,4,5 --budget-mb 11000
  --stop-at 10:00 UTC` (1,405 solves pending). Log: `logs/run_s3.out`, `logs/build.log`.
- 23:50 **Container restart** killed the runner and commit loop (files survived: 479 results,
  all verified; B2's three saved 3-bet SPR 6 states still in `work/state/`). Lost: the solves in
  flight, including single-raised SPR 6 chip-EV solves about 80 minutes in.
- 23:55 Builder change: **mid-solve checkpoints**. Every 15 minutes (between iterations) a
  running solve saves its state to `work/ckpt/<id>.bin` (+ a sidecar with the warm flag and time
  so far); a restarted job resumes from it, and the checkpoint is deleted when the job finishes.
  Tested by killing a solve mid-way: the resumed solve ended with exactly the same iterations and
  exploitability as an uninterrupted one. Results get `"resumed": true` and the total time.
- 10:00 No new solves after this time. 11:05 stopped the runner with one single-raised SPR 6 ICM
  solve still running (`srp-spr60-9s5d2c-rcl-o0i3`): its checkpoint from 11:00 is in
  `work/ckpt/` and the next run resumes it (if the container keeps its disk).
- 11:06 Wrap-up: `verify_results.py` 891 results match the manifest; packed
  `library/LiteLibrary.bin` (1,459 entries, 40,916 bytes, T1 + T2 complete); **test 6 passes**
  (14,590 values, 0 mismatches).

## Task 3: solve times against the pilot

| Tier | Pilot median (s) | Build median so far (s) | Difference |
| --- | --- | --- | --- |
| T1 | 55 | 43 | -22% |
| T2 | 371 | 285 | -23% |
| T3 | 1,208 | 936 | -23% |
| T4 | 113 | 41 | **-64%** (more than 50%: projection updated below) |
| T5 | 3,278 | (no new T5 solves yet) | |

T4's pilot pair was a paired 3-bet SPR 3.5 and a two-tone SRP SPR 1.5 flop; most T4 solves so far
are cheap SPR 1.5 spots. The projection below no longer uses pilot medians: it applies the
measured seconds per MB of every finished solve to every unsolved one (`tools/project_remaining.py`).
New this session: warm single-raised SPR 6 ICM solves were measured (1,700-3,800 s against 6,462 s
cold), which cut the T3/T5 projection by more than half.

## Summary of build session 3

| Tier | Solves done / total | Entries done / total | Solves left | Compute spent (core-h) | Core-h left (projected) | Sessions left |
| --- | --- | --- | --- | --- | --- | --- |
| T1 | 204 / 204 | 336 / 336 | 0 | 3.4 | 0 | 0 |
| T2 | 204 / 204 | 336 / 336 | 0 | 25.7 | 0 | 0 |
| T3 | 127 / 204 | 208 / 336 | 77 | 42.9 | 37 | 0.8 |
| T4 | 352 / 816 | 575 / 1,344 | 464 | 5.8 | 35 | 0.7 |
| T5 | 4 / 408 | 4 / 672 | 404 | 3.9 | 162 | 3.4 |
| **Total** | **891 / 1,836** | **1,459 / 3,024** | **945** | **81.8** | **233** | **4.9** |

- This session solved 460 spots (431 -> 891) in about 43 core-hours, despite losing the
  in-flight solves to the container restart at 23:50.
- Median exploitability over all 891 solves: 0.450% of the pot; all met the 0.5% target, none
  hit the iteration cap, none failed.
- `LiteLibrary.bin`: 40,916 bytes for 1,459 entries.
- **Estimated sessions still needed: about 5** (233 core-hours at 48 per session). Most of it is
  T5's single-raised SPR 6 groups (each: ~2 h chip-EV solve + 16 warm ICM solves of ~0.5-1 h,
  at most 2-3 running at once in the memory budget).

### Notes for the next session
- Start with the same command: `librarybuilder run --jobs 4 --tiers 3,4,5 --budget-mb 11000
  --stop-at <13 h minus 1.5 h>`. It resumes the checkpointed solve and warm-starts from the saved
  states in `work/state/` if the container kept its disk (otherwise those groups' ICM solves run
  cold; the results are unaffected).
- Disk: saved states and checkpoints for single-raised SPR 6 are ~3.7 GB each; 5.9 GB of states
  plus 3.7 GB of checkpoint are on disk now. Chip-EV solves pause under 10 GB free.
- Check the runner with the anchored `pgrep -f "^./build/librarybuilder run"`; after a container
  restart, restart the runner (checkpoints make that cheap).

---

# Build session 4

Start: 2026-10-29 13:36:35 UTC (`date -u`). Wrap-up at 02:36 UTC; no new solves after 01:00 UTC.

## Log
- 13:37 Task 1 (check): build and `make test` pass (8 programs); `verify_results.py`: all 891
  results match the manifest. The checkpoint of `srp-spr60-9s5d2c-rcl-o0i3` and the saved states
  (`srp-spr60-9s5d2c-chip`, `srp-spr35-Ah8d3c-chip`) survived, so the run resumes and warm-starts.
- 13:38 Task 2 (solve): `librarybuilder run --jobs 4 --tiers 3,4,5 --budget-mb 11000
  --stop-at 01:00 UTC` (log `logs/run_s4.out`).
- 13:38-14:00 Commit signing timed out twice (the session's signing service); retried until it
  worked, nothing lost. The commit loop had also stopped at some point; restarted at 15:32.
- 01:16 Main run finished (279 solves, 0 failed). The 11 T3 solves left are single-raised SPR 6
  (up to ~2 h each), too long to start before the wrap-up, so 01:17-02:10 a short extra run
  solved 115 cheap T4 3-bet spots (`--only` T4 3-bet, no new solve after 02:05).
- 02:10 Wrap-up: `verify_results.py` 1,285 results match the manifest; packed
  `library/LiteLibrary.bin` (2,114 entries, 59,256 bytes, T1 + T2 complete); **test 6 passes**
  (21,140 values, 0 mismatches). No checkpoints left; saved warm-start states for three groups
  remain in `work/state/` (used next run if the disk survives).

## Task 3: solve times against the pilot

| Tier | Pilot median (s) | Build median so far (s) | Difference |
| --- | --- | --- | --- |
| T1 | 55 | 43 | -22% |
| T2 | 371 | 285 | -23% |
| T3 | 1,208 | 1,129 | -7% |
| T4 | 113 | 66 | -42% |
| T5 | 3,278 | (no new T5 solves yet) | |

All within 50%; the projection below uses every finished solve (`tools/project_remaining.py`).

## Summary of build session 4

| Tier | Solves done / total | Entries done / total | Solves left | Compute spent (core-h) | Core-h left (projected) | Sessions left |
| --- | --- | --- | --- | --- | --- | --- |
| T1 | 204 / 204 | 336 / 336 | 0 | 3.4 | 0 | 0 |
| T2 | 204 / 204 | 336 / 336 | 0 | 25.7 | 0 | 0 |
| T3 | 193 / 204 | 318 / 336 | 11 | 75.1 | 7.5 | 0.2 |
| T4 | 680 / 816 | 1,120 / 1,344 | 136 | 23.5 | 12.4 | 0.3 |
| T5 | 4 / 408 | 4 / 672 | 404 | 3.9 | 163 | 3.4 |
| **Total** | **1,285 / 1,836** | **2,114 / 3,024** | **551** | **131.6** | **183** | **3.8** |

- This session: 394 solves (891 -> 1,285), about 50 core-hours, 0 failed.
- Median exploitability over all 1,285 solves: 0.459% of the pot; all under 0.5%, none hit the cap.
- `LiteLibrary.bin`: 59,256 bytes for 2,114 entries.
- **Estimated sessions still needed: about 4** (183 core-hours at 48 per session), almost all
  T5 (single-raised SPR 6 on flops 2-3 is the bulk).
- Next session: same command (`--tiers 3,4,5`, stop new solves ~1.5 h before the wrap-up). It
  first finishes T3's 11 single-raised SPR 6 solves, then T4, then T5.

---

# Build session 5

Start: 2026-10-30 10:47:57 UTC (`date -u`). Wrap-up at 23:47 UTC; no new solves after 22:15 UTC.

## Log
- 10:48 Task 1 (check): build and `make test` pass (8 programs); `verify_results.py`: all 1,285
  results match the manifest. Saved warm-start states for 3 groups survived in `work/state/`.
- 10:49 Task 2 (solve): `librarybuilder run --jobs 4 --tiers 3,4,5 --budget-mb 11000
  --stop-at 22:15 UTC` (log `logs/run_s5.out`).
- 12:51 The old background commit loop had died; replaced by a logging committer (pack, commit,
  push every 40 minutes). Commits pushed at 12:51, 14:11, 16:11, 18:11, 20:11 and by hand.
- 16:47 T3 and T4 complete (every T1-T4 spot solved); runner moves on to T5.
- 22:15 No new solves started; the last running solve finished at 22:45 (`222 solved, 0 failed`).
- 22:46 Wrap-up: `verify_results.py` 1,507 results match the manifest, 0 problems; `pack`
  2,476 entries, 69,392 bytes; test 6 PASS (24,760 values, 0 mismatches).

## Task 3: solve times against the pilot

| Tier | Pilot median s | Build median s (all solves so far) | Difference |
| --- | --- | --- | --- |
| T3 | 1,208 | 1,155 | -4% |
| T4 | 113 | 86 | -24% |
| T5 | 3,278 | 1,082 (79 solves) | **-67%** |

T5 differs by more than 50%: the pilot's 4 T5 solves were chip-EV and cold ICM solves, while
most build solves are warm-started ICM solves. The pilot-based projection (`pilot_report.py`,
337 core-hours left) is therefore replaced by the build-rate projection
(`project_remaining.py`, seconds per MB of solver memory per pot type, SPR and mode, measured
on all 1,507 finished solves): **125 core-hours left**.

## Summary of build session 5

| Tier | Solves done / total | Entries done / total | Solves left | Compute spent (core-h) | Core-h left (projected) | Sessions left |
| --- | --- | --- | --- | --- | --- | --- |
| T1 | 204 / 204 | 336 / 336 | 0 | 3.4 | 0 | 0 |
| T2 | 204 / 204 | 336 / 336 | 0 | 25.7 | 0 | 0 |
| T3 | 204 / 204 | 336 / 336 | 0 | 82.9 | 0 | 0 |
| T4 | 816 / 816 | 1,344 / 1,344 | 0 | 33.1 | 0 | 0 |
| T5 | 79 / 408 | 124 / 672 | 329 | 32.4 | 125 | 2.6 |
| **Total** | **1,507 / 1,836** | **2,476 / 3,024** | **329** | **177.6** | **125** | **2.6** |

- This session: 222 solves (1,285 -> 1,507), about 46 core-hours, 0 failed. Tiers T1-T4 are
  complete.
- Median exploitability over all 1,507 solves: 0.461% of the pot; all under 0.5% (T5 max 0.4996%).
- `LiteLibrary.bin`: 69,392 bytes for 2,476 entries.
- **Estimated sessions still needed: about 3** (125 core-hours at 48 per session), all T5.
- Next session: same command (`--tiers 3,4,5` still works; only T5 is left).

---

# Build session 6

Start: 2026-10-30 22:46:23 UTC (`date -u`). Wrap-up at 11:46 UTC on 2026-10-31; no new solves
after 10:15 UTC.

## Log
- 22:46 Task 1 (check): build and `make test` pass (8 programs); `verify_results.py`: all 1,507
  results match the manifest. Saved warm-start states for 3 groups in `work/state/`.
- 22:48 Task 2 (solve): `librarybuilder run --jobs 4 --tiers 3,4,5 --budget-mb 11000
  --stop-at 10:15 UTC` (log `logs/run_s6.out`); only T5 is left (329 solves).
- 02:42 Only 3 of 4 cores were busy: three single-raised SPR 6 solves (3.6 GB each) filled the
  11,000 MB budget, so no fourth solve fit. Restarted the runner with `--budget-mb 12900` (running
  solves resumed from their 1-13 minute old checkpoints). With three single-raised SPR 6 solves
  and one 3-bet SPR 6 solve (1.9 GB) the runner is at 12.6 GB resident, 1.4 GB under the ~14 GB
  kill limit; load average back to 4.
