"""Stage 8: pack the outputs (formats in REPORT.md, "Output formats").

usage: python3 src/pack.py preflop <preflop_prefix> <out_dir>
       python3 src/pack.py postflop <ranges_dir> <solves_dir> <out_dir>

preflop.json   one record per decision node reached with probability > 0.1%
preflop_ev.bin float16, 169 EV values (bb, hand class grid order) per record, record order
postflop/<pottype>_<hero>_vs_<villain>.bin.gz   see REPORT.md
"""
import gzip
import json
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from hands import COMBOS, LABELS, NCOMBOS, RANKS, SUITS  # noqa: E402
from solveio import read_solve  # noqa: E402

SEATS = ["UTG", "UTG1", "LJ", "HJ", "CO", "BTN", "SB", "BB"]
STEP = 5
FREQ_TABLE = list(range(0, 101, STEP))


def quantise(p, step=STEP):
    """Probabilities (summing to 1) -> integer percents in step units summing to 100
    (largest remainder)."""
    units = 100 // step
    x = np.asarray(p, dtype=float)
    x = np.clip(x, 0, None)
    s = x.sum()
    x = x / s if s > 0 else np.full(len(x), 1.0 / len(x))
    raw = x * units
    q = np.floor(raw).astype(int)
    rem = units - q.sum()
    if rem > 0:
        order = np.argsort(-(raw - q), kind="stable")
        q[order[:rem]] += 1
    return q * step


def action_code(a, amount):
    if a == "k":
        return "c"
    if a == "r":
        return f"r{amount:g}"
    return a


def node_id(tree, path, hero):
    vol = [p for p in path if not p.endswith(":f")]
    if not vol:
        return f"{hero}:open"
    return f"{hero}:vs_" + "_".join(p.replace(":", "") for p in vol)


def pack_preflop(prefix, out):
    work = os.path.dirname(prefix) or "."
    tpath = os.path.join(work, "tree.json") if os.path.exists(os.path.join(work, "tree.json")) else "work/tree.json"
    tree = json.load(open(tpath))["nodes"]
    strat = np.fromfile(prefix + ".strat", dtype=np.float32)
    ev = np.fromfile(prefix + ".ev", dtype=np.float32).reshape(len(tree), 169)
    reach = np.fromfile(prefix + ".reach", dtype=np.float64)
    off = 0
    recs, evs, seen = [], [], set()
    for i, n in enumerate(tree):
        if n["kind"] != "dec":
            continue
        k = len(n["actions"])
        S = strat[off:off + k * 169].reshape(k, 169)
        off += k * 169
        if reach[i] <= 0.001:
            continue
        hero = SEATS[n["player"]]
        acts = [action_code(a, x) for a, x in zip(n["actions"], n["amounts"])]
        codes = ["r" if a.startswith("r") else a for a in acts]
        parts = []
        for h in range(169):
            q = quantise(S[:, h])
            items = [f"{codes[a]}{q[a]}" for a in range(k) if codes[a] != "f" and q[a] > 0]
            if items:
                parts.append(f"{LABELS[h]}=" + ",".join(items))
        rid = node_id(tree, n["path"], hero)
        if rid in seen:
            rid = f"{rid}#{i}"
        seen.add(rid)
        recs.append({"id": rid, "path": n["path"], "hero": hero, "actions": acts, "reach": round(float(reach[i]), 6),
                     "node": i, "hands": " ".join(parts)})
        evs.append(ev[i])
    os.makedirs(out, exist_ok=True)
    json.dump({"format": "preflop v1 (REPORT.md)", "hand_classes": LABELS, "records": recs},
              open(os.path.join(out, "preflop.json"), "w"), separators=(",", ":"))
    np.asarray(evs, dtype="<f2").tofile(os.path.join(out, "preflop_ev.bin"))
    print(f"preflop.json: {len(recs)} records; preflop_ev.bin {len(evs) * 169 * 2} bytes")
    return recs


ACT = {1: "fold", 2: "check", 3: "call", 4: "bet", 5: "raise", 6: "allin"}


def act_names(node, pot):
    out = []
    for a, amt in zip(node["actions"], node["amounts"]):
        nm = ACT[a]
        if nm == "bet":
            nm = f"bet{round(100 * amt / pot)}"
        elif nm == "raise":
            nm = f"raise{amt:g}"
        out.append(nm)
    return out


NODE_NAMES = ["oop_root", "ip_vs_check", "ip_vs_bet", "oop_vs_checkbet"]


def pack_postflop(rdir, sdir, out):
    spots = json.load(open(os.path.join(rdir, "spots.json")))
    flops = json.load(open("flops.json"))["flops"]
    os.makedirs(os.path.join(out, "postflop"), exist_ok=True)
    sizes = {}
    for name, sp in spots.items():
        pot_type, hero, _, villain = name.split("_")
        body = bytearray()
        fl = []
        node_hdr = None
        for i, f in enumerate(flops):
            path = os.path.join(sdir, name, f"{i:03d}_{f['flop']}.bin")
            if not os.path.exists(path):
                continue
            s = read_solve(path)
            entry = {"index": i, "flop": f["flop"], "weight": f["weight"], "offset": len(body),
                     "iterations": s["meta"]["iterations"],
                     "exploitability_pct": s["meta"]["exploitability_pct"], "nodes": []}
            r = s["ranges"]
            mx = max(float(r.max()), 1e-12)
            reach_u8 = np.round(r / mx * 255).astype(np.uint8)
            body += reach_u8[0].tobytes() + reach_u8[1].tobytes()
            hdr_nodes = []
            for k, nd in enumerate(s["nodes"]):
                if nd is None:
                    hdr_nodes.append({"name": NODE_NAMES[k], "actions": []})
                    continue
                names = act_names(nd, sp["pot"])
                hdr_nodes.append({"name": NODE_NAMES[k], "player": "oop" if nd["player"] == 0 else "ip",
                                  "actions": names})
                st = nd["strat"]                 # nact x 1326
                inr = s["ranges"][nd["player"]] > 0
                q = np.zeros((1326, len(names)), dtype=np.uint8)
                for c in np.nonzero(inr)[0]:
                    q[c] = quantise(st[:, c]) // STEP
                body += q.tobytes()
            entry["nodes"] = hdr_nodes
            entry["reach_scale"] = mx
            if node_hdr is None:
                node_hdr = hdr_nodes
            fl.append(entry)
        header = {"format": "NLH postflop root decisions v1 (REPORT.md)", "spot": name, "pot_type": pot_type,
                  "hero": hero, "villain": villain, "oop": sp["oop"], "ip": sp["ip"], "pot_bb": sp["pot"],
                  "stack_bb": sp["stack"], "spr": sp["spr"], "donk_limited": bool(sp["donk"]),
                  "freq_table": FREQ_TABLE, "combos": "combos.json order (1326)", "nodes": node_hdr,
                  "flops": fl,
                  "layout": "per flop at offset: uint8 reach[oop][1326], uint8 reach[ip][1326] (0-255, "
                            "scaled to the flop's max weight), then per node with actions: uint8[1326][nact] "
                            "frequency indices into freq_table (sum 20 per in-range combo, 0 = not in range)"}
        hb = json.dumps(header, separators=(",", ":")).encode()
        raw = b"NLHPOST1" + struct.pack("<I", len(hb)) + hb + bytes(body)
        p = os.path.join(out, "postflop", f"{name}.bin.gz")
        with open(p, "wb") as fh:
            fh.write(gzip.compress(raw, compresslevel=9, mtime=0))
        sizes[name] = os.path.getsize(p)
        print(f"{name}: {len(fl)} flops, raw {len(raw)} bytes, gz {sizes[name]} bytes")
    return sizes


def main():
    if sys.argv[1] == "preflop":
        pack_preflop(sys.argv[2], sys.argv[3])
    else:
        pack_postflop(sys.argv[2], sys.argv[3], sys.argv[4])


if __name__ == "__main__":
    main()
