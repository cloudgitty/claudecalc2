"""rake_report.md (output C): what the rake does, spot by spot.

usage: python3 src/rake_report.py <out.md> <main_prefix> <control_prefix> [label=prefix ...]
  <main_prefix>    the 10% / 5bb solution
  <control_prefix> the same tree with rake 0
  extra labelled prefixes (for example cap3=work/s6/cap3) are added as columns, which is how
  the section 5 sensitivity runs (rake_cap 3bb, rake 5%) are reported.

Postflop columns (c-bet frequency on a dry and a wet flop) come from the postflop solves of
each run when <prefix>_solves exists: the first player's betting frequency at the flop root,
weighted by its range, on flop 0 (dry, AcJc8d) and flop 24 (wet, 9c7c6d).
"""
import glob
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from hands import NCOMBOS  # noqa: E402
from preflop_report import summary  # noqa: E402
from solveio import read_solve  # noqa: E402

PRIOR = np.array(NCOMBOS, dtype=float) / 1326.0
SEATS = ["UTG", "UTG1", "LJ", "HJ", "CO", "BTN", "SB"]
DRY, WET = 0, 24


def cbet(solves_dir, spot, idx):
    """C-bet frequency of the preflop raiser: its bet frequency at its first flop decision
    (the root if it is out of position, after the check if it is in position)."""
    paths = glob.glob(os.path.join(solves_dir, spot, f"{idx:03d}_*.bin"))
    if not paths:
        return None
    s = read_solve(paths[0])
    donk = json.load(open(paths[0][:-4] + ".json"))["donk"]
    nd = s["nodes"][1 if donk else 0]
    if nd is None:
        return None
    r = s["ranges"][nd["player"]]
    bets = [i for i, a in enumerate(nd["actions"]) if a in (4, 6)]
    if not bets:
        return None
    f = nd["strat"][bets].sum(0)
    z = r.sum()
    return float((r * f).sum() / z * 100) if z > 0 else None


def main():
    outp = sys.argv[1]
    runs = [("rake 10% / 5bb", sys.argv[2]), ("rake 0 (control)", sys.argv[3])]
    for a in sys.argv[4:]:
        label, p = a.split("=", 1)
        runs.append((label, p))
    S = {}
    for label, p in runs:
        S[label] = summary(p)
    L = [r[0] for r in runs]
    out = ["# Effect of the rake (output C)", "",
           "Widths are % of all 1,326 combos. Every column is a full re-solve of the same tree,",
           "same sizes and same iteration count; only the rake parameters differ.", ""]
    out.append("| Measure | " + " | ".join(L) + " |")
    out.append("| --- | " + " | ".join("---" for _ in L) + " |")
    for s in SEATS:
        out.append(f"| RFI {s} | " + " | ".join(f"{S[l]['rfi'][s]:.1f}" for l in L) + " |")
    out.append("| SB limp | " + " | ".join(f"{S[l]['sb_limp']:.1f}" for l in L) + " |")
    for k in ("BB vs BTN open", "BB vs SB open", "BB vs CO open", "BB vs UTG open"):
        out.append(f"| {k}: call | " + " | ".join(f"{S[l][k]['call']:.1f}" for l in L) + " |")
        out.append(f"| {k}: 3-bet | " + " | ".join(f"{S[l][k]['raise']:.1f}" for l in L) + " |")
    out.append("| BTN vs BB 3-bet: call | " + " | ".join(f"{S[l]['BTN vs BB 3-bet']['call']:.1f}" for l in L) + " |")
    out.append("| BTN vs BB 3-bet: 4-bet | " + " | ".join(f"{S[l]['BTN vs BB 3-bet']['4-bet']:.1f}" for l in L) + " |")
    # postflop c-bet frequencies
    rows = []
    for label, p in runs:
        sd = p + "_solves"
        if not os.path.isdir(sd):
            continue
        for spot in ("srp_BTN_vs_BB", "3bp_BTN_vs_BB"):
            for nm, idx in (("dry AcJc8d", DRY), ("wet 9c7c6d", WET)):
                v = cbet(sd, spot, idx)
                if v is not None:
                    rows.append((f"{spot} c-bet % (preflop raiser), {nm}", label, v))
    if rows:
        out.append("")
        keys = sorted({r[0] for r in rows})
        out.append("| Postflop measure | " + " | ".join(L) + " |")
        out.append("| --- | " + " | ".join("---" for _ in L) + " |")
        for k in keys:
            cells = []
            for l in L:
                v = next((r[2] for r in rows if r[0] == k and r[1] == l), None)
                cells.append("-" if v is None else f"{v:.1f}")
            out.append(f"| {k} | " + " | ".join(cells) + " |")
    # overall
    out += ["", "## Overall", ""]
    for l in L:
        r = S[l]["rfi"]
        tot = sum(r.values())
        out.append(f"- {l}: sum of the 7 RFI widths {tot:.1f} points"
                   f" (UTG {r['UTG']:.1f} ... BTN {r['BTN']:.1f}), SB limp {S[l]['sb_limp']:.1f}")
    base, ctrl = L[0], L[1]
    d = [S[base]["rfi"][s] - S[ctrl]["rfi"][s] for s in SEATS]
    out.append(f"- rake 10%/5bb against the rake-free control: RFI change per seat "
               f"{', '.join(f'{s} {x:+.1f}' for s, x in zip(SEATS, d))}")
    open(outp, "w").write("\n".join(out) + "\n")
    print("\n".join(out[:12]))
    print(f"... written to {outp}")


if __name__ == "__main__":
    main()
