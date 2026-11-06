"""Postflop spots (section 3) and their flop ranges from the preflop solution.

usage: python3 src/spots.py <preflop_prefix> <out_dir>
  writes <out_dir>/<spot>_oop.f32 and _ip.f32 (float32[1326] reach weights) and
  <out_dir>/spots.json (pot, stack, players, donk flag, preflop node id per spot).

A spot is the heads-up flop node reached by its action line, every other player
folding. Ranges are the preflop average-strategy reach of each player along that line
(product of their action probabilities, per hand class, spread evenly over the class's
combos); card removal from the folded players is ignored, as in the preflop model.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from hands import CLASS_COMBOS, NCOMBOS  # noqa: E402

MIN_REACH = 0.01
STACK = float(json.load(open(os.environ.get("NLH_CONFIG", "config.json")))["stack_bb"])
NC = np.array(NCOMBOS, dtype=float)

SEATS = ["UTG", "UTG1", "LJ", "HJ", "CO", "BTN", "SB", "BB"]
SB, BB = 6, 7

# name: (pot type, opener, other, line) with line = actions after the folds, in order.
# Players not named fold when it is their turn.
SPOTS = [
    ("srp_BTN_vs_BB", [("BTN", "r"), ("BB", "c")]),
    ("srp_BTN_vs_SB", [("BTN", "r"), ("SB", "c")]),
    ("srp_CO_vs_BTN", [("CO", "r"), ("BTN", "c")]),
    ("srp_CO_vs_BB", [("CO", "r"), ("BB", "c")]),
    ("srp_HJ_vs_BB", [("HJ", "r"), ("BB", "c")]),
    ("srp_UTG_vs_BB", [("UTG", "r"), ("BB", "c")]),
    ("srp_SB_vs_BB", [("SB", "r"), ("BB", "c")]),
    ("srp_UTG_vs_BTN", [("UTG", "r"), ("BTN", "c")]),
    ("3bp_BTN_vs_BB", [("BTN", "r"), ("BB", "r"), ("BTN", "c")]),
    ("3bp_CO_vs_BTN", [("CO", "r"), ("BTN", "r"), ("CO", "c")]),
    ("3bp_HJ_vs_CO", [("HJ", "r"), ("CO", "r"), ("HJ", "c")]),
    ("3bp_UTG_vs_BB", [("UTG", "r"), ("BB", "r"), ("UTG", "c")]),
    ("3bp_SB_vs_BB", [("SB", "r"), ("BB", "r"), ("SB", "c")]),
    ("4bp_BTN_vs_BB", [("BTN", "r"), ("BB", "r"), ("BTN", "r"), ("BB", "c")]),
    ("4bp_CO_vs_BTN", [("CO", "r"), ("BTN", "r"), ("CO", "r"), ("BTN", "c")]),
]


def postflop_order(p):
    return {SB: 0, BB: 1}.get(p, p + 2)


def load_preflop(prefix, tree_json):
    if not os.path.exists(tree_json):
        tree_json = os.environ.get("NLH_TREE", os.path.join("work", "tree.json"))
    tree = json.load(open(tree_json))["nodes"]
    strat = np.fromfile(prefix + ".strat", dtype=np.float32)
    off, s = 0, {}
    for i, n in enumerate(tree):
        if n["kind"] == "dec":
            k = len(n["actions"])
            s[i] = strat[off:off + k * 169].reshape(k, 169)
            off += k * 169
    assert off == len(strat), (off, len(strat))
    return tree, s


def follow(tree, strat, line):
    """Walk from the root: named players take their action, everyone else folds.
    Returns (terminal node id, reach[8][169])."""
    reach = np.ones((8, 169))
    todo = list(line)
    n = 0
    while tree[n]["kind"] == "dec":
        node = tree[n]
        p = node["player"]
        if todo and SEATS[p] == todo[0][0]:
            want = todo.pop(0)[1]
            idx = next((i for i, a in enumerate(node["actions"]) if a == want or (want == "r" and a == "a")), None)
            if idx is None:
                raise ValueError(f"{SEATS[p]} has no action {want} at node {n}: {node['actions']}")
        else:
            idx = node["actions"].index("f") if "f" in node["actions"] else node["actions"].index("k")
        reach[p] *= strat[n][idx]
        n = node["children"][idx]
    return n, reach


def class_to_combos(r169):
    w = np.zeros(1326, dtype=np.float32)
    for k in range(169):
        w[CLASS_COMBOS[k]] = r169[k]
    return w


def main():
    prefix, out = sys.argv[1], sys.argv[2]
    work = os.path.dirname(prefix) or "."
    tree, strat = load_preflop(prefix, os.path.join(work, "tree.json"))
    os.makedirs(out, exist_ok=True)
    info = {}
    for name, line in SPOTS:
        nid, reach = follow(tree, strat, line)
        t = tree[nid]
        if t["kind"] != "flop" or len(t["players"]) != 2:
            raise SystemExit(f"{name}: line ends at {t['kind']} with players {t.get('players')}")
        a, b = t["players"]
        oop, ip = (a, b) if postflop_order(a) < postflop_order(b) else (b, a)
        inv = t["inv"]
        pot = sum(inv)
        stack = STACK - inv[oop]
        aggr = t["aggressor"]
        donk = int(aggr is not None and aggr != oop)   # OOP is not the preflop raiser
        # DEFAULT: hand classes that reach the flop less than 1% of the time are dropped (the
        # average strategy leaves tiny weights on hands it almost never plays)
        ro_c = np.where(reach[oop] >= MIN_REACH, reach[oop], 0)
        ri_c = np.where(reach[ip] >= MIN_REACH, reach[ip], 0)
        lost = [1 - float((rc * NC).sum() / max((r * NC).sum(), 1e-12)) for rc, r in ((ro_c, reach[oop]), (ri_c, reach[ip]))]
        ro, ri = class_to_combos(ro_c), class_to_combos(ri_c)
        ro.tofile(os.path.join(out, f"{name}_oop.f32"))
        ri.tofile(os.path.join(out, f"{name}_ip.f32"))
        info[name] = dict(node=nid, oop=SEATS[oop], ip=SEATS[ip], pot=pot, stack=stack, spr=stack / pot,
                          donk=donk, aggressor=SEATS[aggr] if aggr is not None else None,
                          pot_type=name.split("_")[0], oop_combos=float(ro.sum()), ip_combos=float(ri.sum()),
                          trimmed_mass=lost, live=[int((ro > 0).sum()), int((ri > 0).sum())])
        print(f"{name:16s} node {nid:5d} pot {pot:6.2f} stack {stack:6.2f} SPR {stack / pot:5.2f} "
              f"OOP {SEATS[oop]} {ro.sum():6.1f} ({(ro > 0).sum()} live, trimmed {lost[0]:.4f}) "
              f"IP {SEATS[ip]} {ri.sum():6.1f} ({(ri > 0).sum()} live, trimmed {lost[1]:.4f}) donk={donk}")
    json.dump(info, open(os.path.join(out, "spots.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
