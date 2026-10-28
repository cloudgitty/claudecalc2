# ICMKit

Swift package for ICM Calc + Ranges, Part B (postflop). See `docs/PART_B_SPEC.md` for the spec,
`ICMKit/PROGRESS.md` for the state of the work and `DECISIONS.md` for design choices.

## Targets

- `ICMCore` (C11, `Sources/ICMCore/`): the engine. Compiles unchanged on Linux (gcc/clang) and
  for iOS (clang). Public headers in `Sources/ICMCore/include/`, umbrella `icmcore.h`:
  - `cards.h` card and combo encoding, parsing
  - `range.h` range notation parser (`"22+,ATs+,KQo:0.5"` -> 1326 weights)
  - `icm.h` Malmuth-Harville ICM (up to 9 players)
  - `handeval.h` 7-card evaluator (lookup tables)
  - `flopclass.h` flop classifier and the spec's representative flops
  - `riskpremium.h` risk premium and stack roles
  - `streetsolver.h` heads-up postflop DCFR solver (flop, turn or river start), chip EV or ICM
  - `libreader.h` Lite library reader (interface only; implemented in Part B2)
  - `f16.h` 16-bit float conversion
- `ICMKit` (Swift, `Sources/ICMKit/`): thin wrappers (`Cards`, `CoreICM`, `StreetSolver`,
  `PostflopLibrary`). Not yet compiled; see the checklist in `PROGRESS.md`.

## Building on Linux or macOS (repo root)

    make             # library, tests, tools into build/
    make test        # fast test suite
    make test-full   # adds the full 133,784,560-hand evaluator enumeration (~5 s)
    make asan        # test suite under AddressSanitizer + UndefinedBehaviorSanitizer

Tools (`tools/`, built into `build/`):
- `buckets [out.json]` risk-premium matchup buckets -> `config/buckets.json`
- `bench [ranges.json] [river|turn|all]` on-device benchmarks
- `estimate [threads] [flop] [spot ...]` flop solve cost per spot type

## Solving a spot (C)

    ss_config c;
    ss_config_default(&c);                 /* spec tree, chips, 0.5% target */
    c.board_len = cards_parse("Ks7d2c4h", c.board, 5);
    static float oop[1326], ip[1326];
    range_parse("22+,A2s+,KTs+,ATo+", oop);
    range_parse("55+,A8s+,KJs+,AJo+", ip);
    c.range[0] = oop; c.range[1] = ip;     /* 0 = out of position */
    c.pot = 10; c.stack = 30;              /* chips, effective stack behind */
    c.mode = SS_MODE_ICM;                  /* optional: ICM payoffs */
    c.icm_players = 6; /* icm_stacks[] = chips behind, icm_payouts[], seat[0], seat[1] */
    c.threads = 4;
    char err[256];
    ss_solver *s = ss_create(&c, err, sizeof err);
    ss_result r;
    ss_solve(s, &r);                       /* ss_cancel(s) from another thread stops it */
    float strat[16 * 1326];
    int k = ss_node_strategy(s, ss_root(s), strat);   /* strat[a * 1326 + combo] */
    ss_destroy(s);
