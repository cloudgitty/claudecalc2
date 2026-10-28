# LiteLibrary.bin format (version 1)

The Lite library ships inside the app. It holds, for every solved library spot, five summary
numbers for the hero under ICM and the same five for the chip-EV baseline. Written by
`tools/librarybuilder pack` (`tools/lb_pack.c`), read by `libreader` (`ICMKit/Sources/ICMCore/libreader.c`)
and in Swift by `PostflopLibrary`. A human-readable copy is written next to it as `LiteLibrary.json`.

All integers are little-endian. Floats in the header are IEEE 754 binary32; values are IEEE 754
binary16 (half precision), converted with the core's own `f16.c`.

## Layout

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | magic `ICMLITE1` (ASCII, no terminator) |
| 8 | 2 | format version = 1 |
| 10 | 2 | header size = 64 |
| 12 | 4 | entry count N |
| 16 | 12 | bucket thresholds t0, t1, t2 (float32, risk premium as a fraction) |
| 28 | 16 | bucket centres c0..c3 (float32) |
| 44 | 1 | tiers complete: bit t-1 set when every spot of tier t is in the file |
| 45 | 1 | values per entry = 10 |
| 46 | 1 | index record size = 8 |
| 47 | 1 | reserved (0) |
| 48 | 4 | index offset = 64 |
| 52 | 4 | values offset = 64 + 8 N |
| 56 | 4 | file size = 64 + 8 N + 20 N |
| 60 | 4 | CRC-32 (zlib / IEEE polynomial) of bytes 64 .. end |
| 64 | 8 N | index, one record per entry |
| 64 + 8 N | 20 N | values, 10 half floats per entry, same order as the index |

Total size: 64 + 28 N bytes. The full library (all 3,024 spots of tiers T1-T5) is 84,736 bytes.

### Index record (8 bytes)

| Byte | Field |
| --- | --- |
| 0 | hero bucket 0..3 |
| 1 | villain bucket 0..3 |
| 2 | hero position: 0 = in position (button), 1 = out of position (big blind) |
| 3 | pot type: 0 = single-raised, 1 = 3-bet |
| 4 | SPR index: 0 = 1.5, 1 = 3.5, 2 = 6 |
| 5 | flop class: 0 dry high, 1 dry low, 2 paired, 3 two-tone, 4 monotone, 5 connected (`flopclass.h`) |
| 6 | flop index 0..2 (representative flop 1..3 of the class, see `docs/PART_B_SPEC.md`) |
| 7 | tier 1..5 |

### Values (10 half floats per entry)

Entries 0-4 are ICM, 5-9 the chip-EV baseline of the same spot, in this order:

| # | Value | Encoding |
| --- | --- | --- |
| 0 | bet % | 0..100: how often hero bets the flop at the first chance (OOP: leading; IP: after a check) |
| 1 | bet size | most-used size there: 0 = 33%, 1 = 75%, 2 = all-in, -1 = hero never bets |
| 2 | check-raise % | 0..100, out of position only; NaN in position |
| 3 | stack-off threshold | weakest hand category that calls an all-in at least 50% of the time on the flop: 0 two pair or better, 1 top pair, 2 middle/bottom pair, 3 strong draw, 4 weak draw, 5 air; -1 none |
| 4 | bluff % | 0..100: share of the betting range (at the bet % node) in weak draws or air |

NaN means "not available" (check-raise in position, or a chip-EV baseline that is not solved yet).
Precision: half floats keep 11 significant bits, so a percentage is exact to about 0.03 points.

## How the values are computed (`tools/lb_summary.c`)

- Hand categories: `handcat.c` (definitions in `DECISIONS.md`), on the flop.
- Bet %, bet size and bluff % use the hero's preflop range weights (hero has not acted yet at
  that node). Bet size is classified by the bet's share of the pot (nearest of 33% / 75%) or
  all-in.
- Check-raise %: after the out-of-position check, for each in-position bet that can be raised,
  the share of the checking range (weighted by how often it checked) that raises or goes
  all-in; averaged over the bet sizes weighted by how often in position bets each size.
- Stack-off: pooled over every flop node where hero faces an all-in, weighted by the
  probability of reaching it with each hand (hero's own reach times the opponent's reach mass).
  Categories are scanned from air upwards; the first whose call frequency is at least 50% is the
  threshold.

## Queries (`lr_query`)

Input: hero premium, villain premium (fractions), position, pot type, SPR, three flop cards,
ICM or chip EV.
1. The flop is mapped to its class with the same classifier; values are averaged over the
   solved representative flops of that class.
2. Premiums are placed between the nearest bucket centres (clamped at the outer centres) and the
   SPR between the nearest of 1.5 / 3.5 / 6; the up to 8 corners are combined with
   bilinear / linear weights. Corners without a solved spot are left out and the weights
   renormalised. Bet size and stack-off category are taken from the corner with the largest
   weight (they are categories, not interpolated).
3. If no corner is solved, the nearest solved spot is used (distance in bucket and SPR steps,
   + 4 for another flop class, + 10 for another pot type, + 20 for another position) and the
   result is marked `LR_SOURCE_NEAREST`.

## Versioning

A reader must reject a file whose magic, version, layout bytes, sizes or CRC do not match.
New fields need a new version number.
