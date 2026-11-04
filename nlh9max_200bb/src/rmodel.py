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
import os
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


def read_rfile_full(path, work="work"):
    nps = {t["id"]: len(t["players"]) for t in read_tree(f"{work}/tree.txt") if t["kind"] == "P"}
    out = []
    with open(path, "rb") as f:
        n = struct.unpack("<i", f.read(4))[0]
        for _ in range(n):
            nid, rho = struct.unpack("<id", f.read(12))
            Rs = [np.frombuffer(f.read(169 * 4), "<f4").copy() for _ in range(nps[nid])]
            out.append((nid, rho, Rs))
    return out


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


def terminal_reach(prefix, work):
    """Reach (8 x 169) of every player at every node under the preflop solution in prefix."""
    tree = json.load(open(f"{work}/tree.json"))["nodes"]
    strat = np.fromfile(prefix + ".strat", dtype=np.float32)
    off, S = 0, {}
    for i, n in enumerate(tree):
        if n["kind"] == "dec":
            k = len(n["actions"])
            S[i] = strat[off:off + k * 169].reshape(k, 169)
            off += k * 169
    reach = {0: np.ones((8, 169))}
    for i, n in enumerate(tree):          # parents come before children in build order
        if n["kind"] != "dec":
            continue
        for a, c in enumerate(n["children"]):
            r = reach[i].copy()
            r[n["player"]] *= S[i][a]
            reach[c] = r
    return tree, reach


def fitted(work, cfg, fitp, prefix, out):
    """R per class for every flop terminal from the fitted spots (src/fit_r.py) and a
    regression for the terminals of position pairs that were not solved."""
    sys.path.insert(0, os.path.dirname(__file__))
    from fit_r import NB, load_eq
    fit = json.load(open(fitp))
    E, P = load_eq(work)
    spots = {}
    for name, f in fit.items():
        spots[name] = f
    # regression samples: per (spot, side) the bucket R with features
    X, Y = [], []
    rake_pts = []
    for name, f in fit.items():
        rake_pts.append((f["pot"], f["rho"] * f["pot"]))
        for seat, sd in f["sides"].items():
            X.append([1.0, sd["ip"], sd["aggressor"], np.log(f["spr"])])
            Y.append(sd["R"])
    X, Y = np.array(X), np.array(Y)
    beta = []
    for b in range(NB):
        ok = np.isfinite(Y[:, b])
        beta.append(np.linalg.lstsq(X[ok], Y[ok, b], rcond=None)[0])
    beta = np.array(beta)
    rake_pts.sort()
    pots = np.array([p for p, _ in rake_pts])
    rakes = np.array([r for _, r in rake_pts])

    def exp_rake(pot):
        r = float(np.interp(pot, pots, rakes, left=rakes[0] * pot / pots[0], right=min(rakes[-1], cfg["rake_cap_bb"])))
        return min(r, cfg["rake_cap_bb"], cfg["rake_pct"] * pot * 3)

    spot_by_node = {}
    rinfo = json.load(open(os.path.join(os.path.dirname(fitp), "spots_used.json"))) if os.path.exists(
        os.path.join(os.path.dirname(fitp), "spots_used.json")) else None
    if rinfo:
        for name, sp in rinfo.items():
            if name in fit:
                spot_by_node[sp["node"]] = name
    tree, reach = terminal_reach(prefix, work)
    entries = []
    nsolved = nreg = 0
    for t in read_tree(f"{work}/tree.txt"):
        if t["kind"] != "P":
            continue
        pl = t["players"]
        pot = t["pot"]
        rho = exp_rake(pot) / pot
        if len(pl) == 3:
            order = sorted(pl, key=postflop_order)
            fac = {order[0]: 0.85, order[1]: 0.92, order[2]: 1.0}
            entries.append((t["id"], rho, [np.full(169, fac[p]) for p in pl]))
            continue
        name = spot_by_node.get(t["id"])
        if name:
            rho = fit[name]["rho"]
        oop, ip = sorted(pl, key=postflop_order)
        r = reach[t["id"]]
        Rs = []
        for p in pl:
            o = ip if p == oop else oop
            eq = (P * E * r[o][None, :]).sum(1) / np.maximum((P * r[o][None, :]).sum(1), 1e-12)
            if name:
                sd = fit[name]["sides"][SEATS[p]]
                edges, Rb = sd["edges"], np.array(sd["R"])
                nsolved += 1
            else:
                stack = cfg["stack_bb"] - t["inv"][p]
                spr = stack / pot
                x = np.array([1.0, float(p == ip), float(t["aggr"] == p), np.log(max(spr, 0.1))])
                Rb = beta @ x
                # bucket edges from this terminal's own range
                w = r[p] * np.array([6 if k // 13 == k % 13 else 4 if k // 13 < k % 13 else 12 for k in range(169)])
                order_ = np.argsort(eq)
                cw = np.cumsum(w[order_])
                cw = cw / cw[-1] if cw[-1] > 0 else np.linspace(0, 1, 169)
                edges = [float(eq[order_][min(np.searchsorted(cw, q), 168)]) for q in np.linspace(0, 1, NB + 1)[1:-1]]
                nreg += 1
            Rb = np.where(np.isfinite(Rb), Rb, np.nanmean(Rb))
            Rs.append(np.clip(Rb[np.searchsorted(edges, eq)], 0.05, 5.0))
        entries.append((t["id"], rho, Rs))
    blend = os.environ.get("R_BLEND")          # "prev_rfile:weight_of_new", damping between rounds
    if blend:
        prevp, wnew = blend.rsplit(":", 1)
        wnew = float(wnew)
        prev = {nid: (rho, Rs) for nid, rho, Rs in read_rfile_full(prevp, work)}
        entries = [(nid, wnew * rho + (1 - wnew) * prev[nid][0],
                    [wnew * np.asarray(R) + (1 - wnew) * np.asarray(Rp) for R, Rp in zip(Rs, prev[nid][1])])
                   for nid, rho, Rs in entries]
        print(f"blended with {prevp}: weight of the new fit {wnew}")
    write_rfile(out, entries)
    print(f"{len(entries)} flop terminals: {nsolved // 2} from solved spots, {nreg // 2} by regression; "
          f"rake points {list(zip(pots.round(1), rakes.round(2)))}")
    print("regression beta per bucket [const, ip, aggressor, log spr]:")
    for b in range(NB):
        print("  ", np.round(beta[b], 3))


SEATS = ["UTG", "UTG1", "LJ", "HJ", "CO", "BTN", "SB", "BB"]


def main():
    mode, work = sys.argv[1], sys.argv[2]
    cfg = json.load(open(os.environ.get("NLH_CONFIG", "config.json")))
    if mode == "fixed":
        fixed(work, cfg, sys.argv[3] if len(sys.argv) > 3 else f"{work}/rfile.bin")
    else:
        fitted(work, cfg, sys.argv[3], sys.argv[4], sys.argv[5] if len(sys.argv) > 5 else f"{work}/rfile.bin")


if __name__ == "__main__":
    main()
