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
