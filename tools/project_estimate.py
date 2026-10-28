# Projects library compute from measured single-thread solve times.
# usage: python3 tools/project_estimate.py docs/estimate/solve_seconds.json
import sys, json
# seconds per solve, single thread: {(pot, spr): {"chips": s, "icm": s, "warm": s}}
T = json.load(open(sys.argv[1]))
flop_factor_f1 = 0.81   # measured: mean ICM solve time over flop 1 of each class / Ks7d2c (3-bet SPR 3.5)
flop_factor_f23 = 0.81  # assumed equal to flop 1 (tree sizes are similar)
cells, mirrored = 14, 12
icm_solves_dedup = mirrored + 2 * (cells - mirrored)                # 16 per (pot, spr, flop)
tiers = {
    "T1": [(1.5, 1)], "T2": [(3.5, 1)], "T3": [(6, 1)],
    "T4": [(1.5, 2), (3.5, 2)], "T5": [(6, 2)],
}
def group_cost(pot, spr, warm, spec_literal):
    t = T[f"{pot}{spr}"]
    if spec_literal:   # 28 ICM entries + 2 chip baselines, every ICM solve cold
        return 2 * t["chips"] + 2 * cells * t["icm"]
    icm = min(t["warm"], t["icm"]) if warm else t["icm"]
    return t["chips"] + icm_solves_dedup * icm
rows = []
for name, parts in tiers.items():
    out = {}
    for mode in ("spec", "dedup", "dedup+warm"):
        s = 0.0
        for spr, flopset in parts:
            nflops = 6 if flopset == 1 else 12
            ff = flop_factor_f1 if flopset == 1 else flop_factor_f23
            for pot in ("srp", "3bp"):
                s += nflops * ff * group_cost(pot, spr if spr != 6 else 6, mode == "dedup+warm", mode == "spec")
        out[mode] = s
    rows.append((name, out))
total = {m: sum(r[1][m] for r in rows) for m in ("spec", "dedup", "dedup+warm")}
print("tier | spec-literal (14 cells, cold) core-h | dedup core-h | dedup + warm core-h | wall h on 4 cores (dedup+warm)")
for name, out in rows + [("total", total)]:
    print(f"{name} | {out['spec']/3600:.1f} | {out['dedup']/3600:.1f} | {out['dedup+warm']/3600:.1f} | {out['dedup+warm']/3600/4:.1f}")
for m in ("spec", "dedup", "dedup+warm"):
    print(m, "sessions (12 usable hours x 4 cores):", round(total[m] / 3600 / 4 / 12, 1))
