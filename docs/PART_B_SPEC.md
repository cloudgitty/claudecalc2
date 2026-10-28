# Part B spec

## Context

ICM Calc + Ranges is an iPhone module (Swift, SwiftUI, iOS 18+) inside the app Poker JARVIS. It calculates ICM equity, bubble factor and preflop ICM ranges. Part B adds deep-stack postflop play:

1. **StreetSolver:** a heads-up postflop CFR solver whose terminal payoffs are valued in tournament prize money (ICM), not chips. The same Swift code runs on the iPhone for turn and river spots and in the cloud for the flop library build.
2. **Lite library:** precomputed flop solutions packed into a file under 1MB that ships inside the app. For each spot it stores a few summary numbers for ICM and for chip EV.

UI is out of scope.

## Ground rules

- **Language: Swift**, in a Swift package named `ICMKit`. Extend it if it exists; otherwise create it.
- **Core modules must compile on Linux and iOS.** No Apple-only frameworks in core code (no Accelerate, no UIKit). Use Swift `SIMD` types and plain loops. Apple-only speed-ups may sit behind `#if canImport(Accelerate)`.
- **Write all solver code yourself.** Do not copy or adapt code from TexasSolver, postflop-solver, WASM Postflop or any GPL/AGPL project. The output ships in a commercial app.
- **No gambling features.** This is a math and study tool.

## Modules

| Module | Job |
| --- | --- |
| `ICMEngine` | Malmuth-Harville ICM equity for up to 9 players. Reuse unchanged if it exists. |
| `HandEvaluator` | Fast 7-card hand evaluator, own implementation (lookup-table based). |
| `RiskPremium` | Risk premium for any pair of players at a table: the extra equity a player needs under ICM, on top of chip EV, to call an all-in against that opponent. |
| `StreetSolver` | Heads-up postflop solver, flop to river, with ICM terminal payoffs. |
| `LibraryBuilder` | Command-line tool that generates the spots, runs solves in parallel and packs results. Linux and macOS only, not shipped in the app. |
| `PostflopLibrary` | Reader for the packed library file, used by the app. iOS-compatible. |

## StreetSolver

- **Algorithm:** Discounted CFR (or CFR+), combo-level ranges (up to 1,326 combos, minus board blockers), suit isomorphism, chance nodes for turn and river cards, O(n) river showdown evaluation using sorted hand strength. Float32.
- **Tree, every street:** bet 33% pot, bet 75% pot, all-in. Facing a bet: fold, call, raise to 3× the bet, all-in. At most one raise per street, then all-in or call only. Any bet or raise that would leave less than 67% of the pot behind becomes all-in. Both players have the same options; out-of-position acts first.
- **Payoffs:** each terminal chip state (hero stack, villain stack, all other stacks fixed) is valued in $ with `ICMEngine` against the spot's full table and payouts. Each player maximizes their own $ EV. The game is not zero-sum, so report each player's best-response gain as their exploitability.
- **Chip EV mode:** same solver with payoffs equal to chips won or lost.
- **Stop rule:** exploitability under 0.5% of the pot, or a configurable iteration cap.
- **On-device use:** the same API solves a single turn or river spot. iPhone 13 targets: river under 2 seconds, turn plus river under 20 seconds.

## Matchups by risk premium

Two canonical 6-handed final tables, payouts 40 / 25 / 15 / 10 / 6 / 4 % of the prize pool:

- **Runaway chip leader table**, stacks in big blinds: 100, 40, 30, 25, 15, 10
- **Chip leader table**, stacks in big blinds: 60, 50, 35, 30, 15, 10

Stack roles (labels only): short stack = under 0.6× the average stack or 15bb or less; chip leader = largest stack; runaway chip leader = largest stack and either at least 2× the second-largest stack or at least 40% of all chips; mid stack = everyone else.

1. For every ordered pair of players at both tables, compute each player's risk premium against the other.
2. Split all hero risk-premium values into 4 buckets at the quartiles.
3. The library is indexed by (hero bucket, villain bucket), up to 16 cells. For each cell, use the real player pair whose premiums sit closest to the cell's centre. A cell with no real pair is skipped; the app falls back to the nearest cell.

## Other dimensions

| Dimension | Values |
| --- | --- |
| Hero position | In position (button), out of position (big blind) |
| Pot type | Single-raised: button opens 2.2bb, big blind calls. 3-bet: button opens 2.2bb, big blind 3-bets to 7bb, button calls. |
| Stack-to-pot ratio | 1.5, 3.5, 6. Effective stacks are set from the pot to hit these (single-raised pot about 6bb, 3-bet pot about 15bb including blinds and antes). Other stacks keep their canonical values. |
| Flop class | 6 classes, 3 representative flops each |

**Flop classifier** (first rule that matches wins): paired → monotone → connected (no pair, highest minus lowest rank 4 or less) → two-tone → dry high (rainbow, highest card T or above) → dry low.

| Class | Flop 1 | Flop 2 | Flop 3 |
| --- | --- | --- | --- |
| Dry high | Ks 7d 2c | Ah 8d 3c | Qs 6d 2h |
| Dry low | 9s 5d 2c | 8h 4d 3c | 7s 3d 2h |
| Paired | Ks Kd 4c | 8s 8d 3h | 5s 5d Ac |
| Two-tone | As Js 5d | Qh 9h 4c | Td 6d 2s |
| Monotone | Ks 9s 4s | Jh 7h 3h | Ad Td 5d |
| Connected | 9s 8d 7c | Js Td 8h | 6s 5d 4h |

## Build tiers

The library is solved in tiers, so every session leaves a usable library. Each tier includes its chip EV baselines (one per position, pot type, SPR and flop, not per matchup).

| Tier | Spots | Approx. solves |
| --- | --- | --- |
| T1 | SPR 1.5, flop 1 of each class | 384 ICM + 24 chip EV |
| T2 | SPR 3.5, flop 1 of each class | 384 ICM + 24 chip EV |
| T3 | SPR 6, flop 1 of each class | 384 ICM + 24 chip EV |
| T4 | SPR 1.5 and 3.5, flops 2 and 3 | 1,536 ICM + 96 chip EV |
| T5 | SPR 6, flops 2 and 3 | 768 ICM + 48 chip EV |

Counts assume all 16 matchup cells exist; skipped cells reduce them.

## Preflop ranges

- If `ICMKit` contains `PreflopSolver`, use it to generate ranges for each canonical table, stack depth and line.
- Otherwise write placeholder ranges in `ranges/` (one JSON file per pot type and stack depth, standard range notation), based on typical tournament ranges at those depths. Mark every file `"placeholder": true` and list them in `DECISIONS.md`.

## What each spot stores

For the hero, for ICM and for the matching chip EV baseline:

1. **Bet %:** how often hero bets the flop when they can bet first (in position: after a check; out of position: leading).
2. **Bet size:** hero's most-used size (33%, 75% or all-in).
3. **Check-raise %:** out of position only; empty in position.
4. **Stack-off threshold:** the weakest hand category that gets all-in at least 50% of the time when facing an all-in on the flop.
5. **Bluff %:** share of hero's betting range in the weak draw or air categories.

**Hand categories:** sets and better / two pair; top pair; middle and bottom pair; strong draws (8 or more outs); weak draws; air.

**Raw strategies:** also save each solve's flop decision points, quantized to 8 bits and compressed, in `raw/`, at most 50KB per solve. Not shipped; kept for a fuller library later.

## Running builds

- **Parallelism:** 4 solves at a time by default, each limited to about 3GB. Drop to 3 if memory runs short.
- **Resumable:** a manifest lists every spot with its tier. Each result is written atomically to `results/<spotId>.json.gz` and committed. Finished spots are skipped on restart.
- **Order:** solve tiers in order T1 → T5.
- **Logging:** one line per finished solve: spot id, tier, iterations, exploitability, seconds.

## Packing and the reader

- Pack all finished results into `LiteLibrary.bin`: a header with format version, bucket thresholds and which tiers are complete, an index, then values as Float16. Under 1MB.
- Also export `LiteLibrary.json` for inspection.
- `PostflopLibrary` answers queries by (hero premium, villain premium, position, pot type, SPR, flop). It maps any flop to its class with the same classifier, interpolates between the nearest premium buckets and SPRs, and falls back to the nearest solved spot when a spot is missing.
- Document the format in `LIBRARY_FORMAT.md`.

## Tests

1. **Hand evaluator:** enumerate all 133,784,560 seven-card hands and match the standard category counts: straight flush 41,584; four of a kind 224,848; full house 3,473,184; flush 4,047,644; straight 6,180,020; three of a kind 6,461,620; two pair 31,433,400; one pair 58,627,800; high card 23,294,460.
2. **ICM engine:** matches a brute-force ICM calculation on random tables of 2 to 9 players. Equities sum to the prize pool.
3. **Toy river game:** polarized range (nuts and air) against bluff-catchers, one bet size s (as a fraction of pot). The bluff share of the betting range is within 1 percentage point of s ÷ (1 + 2s), and the defender's call frequency within 1 point of 1 ÷ (1 + s).
4. **ICM sanity:** with winner-take-all payouts, ICM mode matches chip EV mode within solver tolerance.
5. **Convergence:** exploitability reaches the target on 5 sample spots of each pot type.
6. **Round trip:** every value read back from `LiteLibrary.bin` matches its source result within Float16 precision.
