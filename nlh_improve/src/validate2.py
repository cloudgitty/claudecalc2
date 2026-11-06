"""Extra validation checks of the improvement plan, appended to validation.md (Phase D).

usage (from a build folder): python3 ../nlh_improve/src/validate2.py [validation.md]

 1 payoff conservation: a short preflop solve on the final tree with rake 0 and R = 1; the eight
   seats' EVs must sum to 0 within 0.02 bb per hand
 2 convergence log: the main loop and every rake variant must show RFI moves <= 0.5 points for two
   rounds in a row (from work2/pipeline2.log)
 3 river error: on the river-sampled flops, the correction to R (river R / turn-end R) and the
   change of the four root strategies between the turn-end and the river solves (mean absolute
   frequency difference, reach-weighted, in points)
"""
import glob
import json
import os
import re
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from rmodel import read_tree, write_rfile  # noqa: E402
from solveio import read_solve  # noqa: E402

TOL_CONS = 0.02


def conservation(lines):
    d = "work2/cons"
    os.makedirs(d, exist_ok=True)
    rf = f"{d}/rfile_R1.bin"
    write_rfile(rf, [(t["id"], 0.0, [np.ones(169) for _ in t["players"]])
                     for t in read_tree("work2/tree.txt") if t["kind"] == "P"])
    if not os.path.exists(f"{d}/pre.log"):
        subprocess.run(f"{os.path.dirname(HERE)}/build/preflop work2 300 {d}/pre {rf} 4 0 0", shell=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    last = [x for x in open(f"{d}/pre.log") if x[0].isdigit()][-1]
    s = float(re.search(r"sum EV (-?[0-9.]+)", last).group(1))
    ok = abs(s) <= TOL_CONS
    lines.append(f"- {'PASS' if ok else '**FAIL**'} payoff conservation (rake 0, R = 1, 300 iterations): "
                 f"sum of the 8 seats' EVs {s:+.4f} bb per hand (tolerance {TOL_CONS}; the original model "
                 f"gave +0.066 at 100bb)")
    return ok


def convergence(lines):
    log = open("work2/pipeline2.log").read() if os.path.exists("work2/pipeline2.log") else ""
    ok_all = True
    for tag in ("main", "rake0", "cap3", "pct5"):
        # a resumed pipeline logs finished rounds again: keep the last line per round
        per = {int(k): float(m) for k, m in re.findall(rf"{tag} round (\d+): largest RFI move ([0-9.]+)", log)}
        moves = [per[k] for k in sorted(per)]
        rounds = len(moves)
        conv = any(a <= 0.5 and b <= 0.5 for a, b in zip(moves, moves[1:]))
        ok_all &= conv
        lines.append(f"- {'PASS' if conv else '**FAIL**'} convergence of `{tag}`: {rounds} rounds, largest RFI "
                     f"moves {', '.join(f'{m:.2f}' for m in moves)}")
    return ok_all


def river_error(lines):
    if not os.path.exists("work2/river/corr.json"):
        lines.append("- river error: no river correction in this run")
        return True
    corr = json.load(open("work2/river/corr.json"))
    for r in corr["report"]:
        lines.append(f"  - {r}")
    diffs = []
    for p in sorted(glob.glob("work2/river/river/*/*.bin")):
        q = p.replace("/river/river/", "/river/turn/")
        if not os.path.exists(q):
            continue
        a, b = read_solve(q), read_solve(p)
        for na, nb in zip(a["nodes"], b["nodes"]):
            if na is None or nb is None or na["strat"].shape != nb["strat"].shape:
                continue
            w = a["ranges"][na["player"]]
            if w.sum() <= 0:
                continue
            d = np.abs(na["strat"] - nb["strat"]).sum(0) / 2       # total variation per combo
            diffs.append(float((w * d).sum() / w.sum()) * 100)
    if diffs:
        lines.append(f"- river error on the root strategies: mean {np.mean(diffs):.1f} points, worst node "
                     f"{max(diffs):.1f} points of frequency moved between the turn-end and the river solves "
                     f"({len(diffs)} root nodes); the stored strategies are turn-end solves")
    return True


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "validation.md"
    lines = ["", "## 7 Improvement-plan checks", ""]
    c1 = conservation(lines)
    c2 = convergence(lines)
    river_error(lines)
    with open(out, "a") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0 if c1 and c2 else 1


if __name__ == "__main__":
    sys.exit(main())
