"""Summary numbers of a preflop solution (widths, hand sanity table).

usage: python3 src/preflop_report.py <preflop_prefix> [--json out.json]

Widths are in % of all 1,326 combos (hand classes weighted by their combo counts).
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from hands import LABELS, NCOMBOS  # noqa: E402
from spots import SEATS, follow, load_preflop  # noqa: E402

PRIOR = np.array(NCOMBOS, dtype=float) / 1326.0
SANITY = ["AA", "KK", "AKs", "AKo", "22", "87s", "A5s", "K9o", "72o"]


def node_after(tree, strat, line):
    """Decision node reached when the named players take their actions and the others fold."""
    n = 0
    todo = list(line)
    while tree[n]["kind"] == "dec":
        node = tree[n]
        p = SEATS[node["player"]]
        if not todo:
            return n
        if p == todo[0][0]:
            want = todo.pop(0)[1]
            idx = next(i for i, a in enumerate(node["actions"]) if a == want or (want == "r" and a == "a"))
        else:
            idx = node["actions"].index("f")
        n = node["children"][idx]
    return n


def width(strat_node, actions, which, reach=None):
    """% of all combos taking the actions; with reach, % of the combos that reach the node."""
    idx = [i for i, a in enumerate(actions) if a in which]
    f = strat_node[idx].sum(axis=0)
    if reach is None:
        return float((PRIOR * f).sum() * 100), f
    z = (PRIOR * reach).sum()
    return float((PRIOR * reach * f).sum() / z * 100) if z > 0 else 0.0, f


def actor_reach(tree, strat, n):
    """Reach of the acting player at node n along its path (own strategy only)."""
    path = []
    m = n
    parent = {c: (i, k) for i, t in enumerate(tree) if t["kind"] == "dec" for k, c in enumerate(t["children"])}
    while m in parent:
        i, k = parent[m]
        path.append((i, k))
        m = i
    p = tree[n]["player"]
    r = np.ones(169)
    for i, k in path:
        if tree[i]["player"] == p:
            r *= strat[i][k]
    return r


def summary(prefix):
    work = os.path.dirname(prefix) or "."
    tree, strat = load_preflop(prefix, os.path.join(work, "tree.json"))
    out = {"rfi": {}, "sanity": {}}
    for s in SEATS[:-1]:
        line = [(s, None)]
        n = node_after(tree, strat, [])
        # walk folds to seat s
        n = 0
        while SEATS[tree[n]["player"]] != s:
            n = tree[n]["children"][tree[n]["actions"].index("f")]
        acts = tree[n]["actions"]
        w, f = width(strat[n], acts, ("r", "a"))
        out["rfi"][s] = w
        if s == "SB":
            out["sb_limp"] = width(strat[n], acts, ("c",))[0]
        out["sanity"][f"{s} RFI"] = {h: {a: round(float(strat[n][i][LABELS.index(h)]) * 100, 1)
                                          for i, a in enumerate(acts)} for h in SANITY}
        del line
    # BB vs BTN open, BB vs SB open, BTN vs CO open
    for name, line, who in [("BB vs BTN open", [("BTN", "r")], "BB"), ("BB vs SB open", [("SB", "r")], "BB"),
                            ("BB vs CO open", [("CO", "r")], "BB"), ("BB vs UTG open", [("UTG", "r")], "BB"),
                            ("SB vs BTN open", [("BTN", "r")], "SB"), ("BTN vs CO open", [("CO", "r")], "BTN"),
                            ("BTN vs UTG open", [("UTG", "r")], "BTN"), ("CO vs HJ open", [("HJ", "r")], "CO")]:
        n = node_after(tree, strat, line)
        while SEATS[tree[n]["player"]] != who:
            n = tree[n]["children"][tree[n]["actions"].index("f")]
        acts = tree[n]["actions"]
        out[name] = {"call": width(strat[n], acts, ("c",))[0], "raise": width(strat[n], acts, ("r", "a"))[0],
                     "node": n}
        if name == "BB vs BTN open":
            out["sanity"][name] = {h: {a: round(float(strat[n][i][LABELS.index(h)]) * 100, 1)
                                       for i, a in enumerate(acts)} for h in SANITY}
    # BTN vs BB 3-bet, BTN 4-bet range
    n = node_after(tree, strat, [("BTN", "r"), ("BB", "r")])
    acts = tree[n]["actions"]
    r = actor_reach(tree, strat, n)
    out["BTN vs BB 3-bet"] = {"call": width(strat[n], acts, ("c",), r)[0], "4-bet": width(strat[n], acts, ("r", "a"), r)[0],
                              "(of BTN opens)": 100.0}
    n = node_after(tree, strat, [("CO", "r"), ("BTN", "r")])
    while SEATS[tree[n]["player"]] != "CO":
        n = tree[n]["children"][tree[n]["actions"].index("f")]
    acts = tree[n]["actions"]
    r = actor_reach(tree, strat, n)
    out["CO vs BTN 3-bet"] = {"call": width(strat[n], acts, ("c",), r)[0], "4-bet": width(strat[n], acts, ("r", "a"), r)[0],
                              "(of CO opens)": 100.0}
    return out


def main():
    prefix = sys.argv[1]
    s = summary(prefix)
    print("RFI widths (% of combos):", " ".join(f"{k} {v:.1f}" for k, v in s["rfi"].items()),
          f"| SB limp {s['sb_limp']:.1f}")
    for k in s:
        if k in ("rfi", "sanity", "sb_limp"):
            continue
        print(f"{k}: " + ", ".join(f"{a} {b:.1f}" for a, b in s[k].items() if a != "node"))
    if "--json" in sys.argv:
        json.dump(s, open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=1)


if __name__ == "__main__":
    main()
