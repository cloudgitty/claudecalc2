"""Realisation factors R and expected rake per flop terminal of the preflop tree.

usage:
  python3 src/rmodel.py fixed <work_dir> [out]          stage 3: fixed priors
  python3 src/rmodel.py fitted <work_dir> <fit.json> [out]   stage 5+: fitted model

Writes the rfile read by src/preflop.c: int32 count, then per flop terminal
(int32 node id, float64 rho, np x 169 float32 R) with players in tree order.

Fixed priors (stage 3, before any postflop solve): R = 1.0 in position and 0.85 out of
position heads-up; 3-way 0.85 / 0.92 / 1.0 by postflop acting order; flat over hands;
rho = min(rake_pct x pot, cap) / pot (the rake on the flop pot, ignoring later growth).
The fitted model replaces all of these with numbers measured from postflop solves.
"""
import json
import struct
import sys

import numpy as np

SB, BB = 6, 7


def postflop_order(p):
    return {SB: 0, BB: 1}.get(p, p + 2)


def read_tree(path):
    nodes = []
    with open(path) as f:
        n = int(f.readline())
        for line in f:
            t = line.split()
            nid, kind = int(t[0]), t[1]
            if kind in "PA":
                ptype, aggr, np_ = int(t[2]), int(t[3]), int(t[4])
                players = [int(x) for x in t[5:5 + np_]]
                inv = [float(x) for x in t[5 + np_:5 + np_ + 8]]
                nodes.append(dict(id=nid, kind=kind, ptype=ptype, aggr=aggr, players=players, inv=inv,
                                  pot=sum(inv)))
    return nodes


def write_rfile(path, entries):
    with open(path, "wb") as f:
        f.write(struct.pack("<i", len(entries)))
        for nid, rho, Rs in entries:
            f.write(struct.pack("<id", nid, rho))
            for R in Rs:
                f.write(np.asarray(R, dtype="<f4").tobytes())


def fixed(work, cfg, out):
    entries = []
    for n in read_tree(f"{work}/tree.txt"):
        if n["kind"] != "P":
            continue
        order = sorted(n["players"], key=postflop_order)
        if len(order) == 2:
            fac = {order[0]: 0.85, order[1]: 1.0}
        else:
            fac = {order[0]: 0.85, order[1]: 0.92, order[2]: 1.0}
        rho = min(cfg["rake_pct"] * n["pot"], cfg["rake_cap_bb"]) / n["pot"]
        entries.append((n["id"], rho, [np.full(169, fac[p]) for p in n["players"]]))
    write_rfile(out, entries)
    print(f"{len(entries)} flop terminals written to {out}")


def main():
    mode, work = sys.argv[1], sys.argv[2]
    cfg = json.load(open("config.json"))
    if mode == "fixed":
        fixed(work, cfg, sys.argv[3] if len(sys.argv) > 3 else f"{work}/rfile.bin")
    else:
        raise SystemExit("fitted mode is added in stage 5")


if __name__ == "__main__":
    main()
