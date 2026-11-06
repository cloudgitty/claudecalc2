"""River correction for R (improvement plan, item 8).

usage: python3 src/river_corr.py <fit_turn.json> <fit_river.json> <out.json>

Both fits come from src/fit_r.py on the SAME spots, flops and ranges: one with the usual turn-end
equity terminal, one solved flop to river. Per spot and side the correction is the ratio of the
river R to the turn-end R:
  per strength bucket: C_b = R_river_b / R_turn_b
  per hand class:      C_h = (w_h * R_river_h / R_turn_h + K * C_b(h)) / (w_h + K), K = 6 combos
and, for position pairs without river solves, the reach-weighted mean C_b per group
(pot type, in position, preflop aggressor). src/rmodel.py multiplies R by these when RIVER_CORR
points at the output. The report lines give the size of the correction: how much the turn-end
model mis-states R.
"""
import json
import sys

import numpy as np

K = 6.0
NB = 5


def main():
    turn, river = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
    out = {"spots": {}, "groups": {}, "report": []}
    groups = {}
    for name in sorted(set(turn) & set(river)):
        pt = name.split("_")[0]
        out["spots"][name] = {}
        for seat, st in turn[name]["sides"].items():
            sr = river[name]["sides"].get(seat)
            if sr is None or "R_class" not in st or "R_class" not in sr:
                continue
            Rt, Rr = np.array(st["R"], float), np.array(sr["R"], float)
            Cb = np.where(np.isfinite(Rt) & np.isfinite(Rr) & (Rt > 0), Rr / np.where(Rt > 0, Rt, 1), 1.0)
            Ct, Cr = np.array(st["R_class"]), np.array(sr["R_class"])
            w = np.array(sr.get("w_class", st.get("w_class", np.zeros(169))))
            b = np.searchsorted(st["edges"], np.array(st["eq_class"]))
            Cbh = Cb[np.clip(b, 0, NB - 1)]
            ratio = np.where(Ct > 0, Cr / np.where(Ct > 0, Ct, 1), Cbh)
            Ch = (w * ratio + K * Cbh) / (w + K)
            out["spots"][name][seat] = {"C_class": [float(x) for x in Ch], "C_bucket": [float(x) for x in Cb]}
            key = f"{pt}|{st['ip']}|{st['aggressor']}"
            groups.setdefault(key, []).append((Cb, float(w.sum())))
            m = float((w * Ch).sum() / w.sum()) if w.sum() > 0 else 1.0
            out["report"].append(f"{name} {seat}: mean correction {m:.3f}, buckets "
                                 + " / ".join(f"{x:.2f}" for x in Cb)
                                 + f"; realised share turn-end {st['realised_share']:.3f} river {sr['realised_share']:.3f}")
    for key, lst in groups.items():
        W = sum(w for _, w in lst)
        out["groups"][key] = [float(sum(c[k] * w for c, w in lst) / W) for k in range(NB)]
    json.dump(out, open(sys.argv[3], "w"), indent=1)
    print("\n".join(out["report"]))
    for k, v in sorted(out["groups"].items()):
        print(f"group {k}: " + " / ".join(f"{x:.2f}" for x in v))


if __name__ == "__main__":
    main()
