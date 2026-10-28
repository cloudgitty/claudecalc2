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
  1.5 (all within 0.1 points). Test 4 passes (see below). `test_solver` checks tree rules, zero-sum
  chip values, isomorphism, thread determinism, cancel and the memory limit.
- 00:51 All tests (`make asan`) pass under AddressSanitizer + UndefinedBehaviorSanitizer
  (LeakSanitizer on): no reports.
- 00:55 Task 7: Swift layer written, **not yet compiled** (no Swift toolchain here):
  `ICMKit/Sources/ICMKit/Cards.swift`, `CoreICM.swift`, `StreetSolver.swift`, `PostflopLibrary.swift`.
  See "Swift layer: first Xcode build checklist" below.

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
- 02:45 Re-ran `make asan` on the final engine (16-bit storage, warm start, hand categories
  added since 00:51): all 7 test programs pass under ASan + UBSan (+ LeakSanitizer), no reports.
