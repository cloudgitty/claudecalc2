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
