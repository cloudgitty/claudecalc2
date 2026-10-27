# PokerTrainer export: PLO6 6-max, 200bb

| file | size | content |
|---|---|---|
| `plo6_ranges_packed_200bb.json` | 1.81 MB | `{"n":962988,"planes":5,"classes":…,"spots":{"plo6_6max_200bb/<SPOT>":…}}` |
| `widths_200bb.json` | 1 KB | `{"<SPOT>":[raisePctOfAllHands, continuePctOfAllHands]}` |
| `test_vectors_200bb.json` | 0.18 MB | 300 hands: `{"cards":[…], "code":…, "id":…, "bytes":{"<SPOT>":…}}` |

Only the 200bb stack exists; no 100bb or 300bb PLO6 ranges have been computed.

**Spot mapping.** The source uses exactly the spec's 24 names, so no renaming was needed:
`LJ_RFI HJ_RFI CO_RFI BTN_RFI SB_RFI`, `BB_vs_LJ BB_vs_HJ BB_vs_CO BB_vs_BTN BB_vs_SB SB_vs_CO SB_vs_BTN BTN_vs_CO`,
`LJ_vs_BB3bet HJ_vs_BB3bet CO_vs_BB3bet BTN_vs_BB3bet SB_vs_BB3bet CO_vs_BTN3bet BTN_vs_SB3bet`,
`BB_vs_BTN4bet BB_vs_SB4bet SB_vs_BTN4bet BTN_vs_CO4bet`.
Every key in the packed file is prefixed `plo6_6max_200bb/`. The widths and test-vector keys use the bare names.

**Byte meaning.** `byte = offset(raise/5) + call/5`, `offset(r) = sum_{k<r}(21-k)`, values 0..230.
Frequencies are conditional on reaching the decision. A class that never reaches it (for example a hand
outside the opener's range in an "opener vs 3-bet" spot) or always folds is 0.

**Checks run** (`src/check_app_export.py`, an independent decoder written from the format spec):
- n = 962,988; codes sorted and unique; combos sum to 20,358,520 (asserted at export).
- 24 spots, all keys as specified; max byte 230; every byte decodes to call + raise <= 100.
- 100,000 random (class, spot) pairs vs the source: 0 mismatches.
- 300 test vectors replayed with the reference class-code algorithm: 0 failures.
- The packed file is 1.81 MB, under the 6 MB target.

Reproduce: `cd src && python3 export_app.py && python3 check_app_export.py`. This needs `work/`
from the pipeline run. Read `../REPORT.md` for how the ranges were made and their known weak points.
