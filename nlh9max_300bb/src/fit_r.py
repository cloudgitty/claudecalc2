"""Stage 5: measure the realisation factor R and the expected rake from postflop solves.

usage: python3 src/fit_r.py <ranges_dir> <solves_dir> <out.json>

For every solved spot (heads-up, flop root) and side:
  V(h)   = flop-weighted, reach-weighted mean of the per-combo EV over the combos of class h
           (chips from the flop on, rake included) - the hand's realised value of the pot;
  rho    = 1 - (E[V_oop] + E[V_ip]) / P, the expected rake as a fraction of the flop pot P.
R per hand class is the ratio of the realised share to the equity share of the net pot:
  R(h) = V(h) / ((1 - rho) P e(h)),  e(h) = equity of h against the opponent's flop range,
which is the preflop model's heads-up terminal value R(h) e(h) (1 - rho) P (section 2.4). Classes are grouped into 5 strength buckets by their
equity against the opponent's flop range (reach-weighted quintiles), and the bucket value is
the reach-weighted mean of the class R. Output: per spot and side the bucket edges and R, rho,
and the features used by the regression in src/rmodel.py.
"""
import glob
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from hands import CLASS_COMBOS, COMBOS, NCOMBOS  # noqa: E402
from solveio import read_solve  # noqa: E402

NB = 5
K_SHRINK = 6.0   # prior weight (combos) of the bucket R in the per-class R
SEATS = ["UTG", "UTG1", "LJ", "HJ", "CO", "BTN", "SB", "BB"]


def load_eq(work="work"):
    E = np.fromfile(f"{work}/eq169.bin", np.float64, 169 * 169).reshape(169, 169)
    N = np.fromfile(f"{work}/eq169.bin", np.int32, offset=169 * 169 * 8).reshape(169, 169).astype(float)
    P = N / N.sum(1, keepdims=True)
    return E, P


def card_mask(flop):
    from hands import RANKS, SUITS
    cards = [RANKS.index(flop[i]) * 4 + SUITS.index(flop[i + 1]) for i in (0, 2, 4)]
    return np.array([not (a in cards or b in cards) for a, b in COMBOS])


def model_share(Ri, Rj, E, P, rj, net_frac):
    """Model pot share per class for side i (169), given the opponent class reach rj."""
    a = Ri[:, None] * E
    b = Rj[None, :] * (1 - E)
    share = np.where(a + b > 0, a / np.maximum(a + b, 1e-12), 0.5)
    w = P * rj[None, :]
    z = w.sum(1)
    return net_frac * (w * share).sum(1) / np.maximum(z, 1e-12)


def fit_spot(name, info, solves_dir, flops, E, P):
    files = sorted(glob.glob(os.path.join(solves_dir, name, "*.bin")))
    if not files:
        return None
    V = np.zeros((2, 169))
    W = np.zeros((2, 169))
    ev_side = np.zeros(2)
    wsum_side = np.zeros(2)
    expl = []
    fw_tot = 0
    for fpath in files:
        idx = int(os.path.basename(fpath).split("_")[0])
        fw = flops[idx]["weight"]
        try:
            s = read_solve(fpath)
        except json.JSONDecodeError:
            print(f"{name}: skipped {os.path.basename(fpath)} (non-finite solve result)")
            continue
        if not all(np.isfinite(s["meta"]["exploitability_pct"])):
            print(f"{name}: skipped {os.path.basename(fpath)} (non-finite solve result)")
            continue
        m = card_mask(flops[idx]["flop"])
        expl.append(max(s["meta"]["exploitability_pct"]))
        fw_tot += fw
        for side in range(2):
            r = s["ranges"][side] * m
            ev = s["ev"][side]
            ev_side[side] += fw * (r * ev).sum() / max(r.sum(), 1e-12)
            wsum_side[side] += fw
            for k in range(169):
                cc = CLASS_COMBOS[k]
                V[side, k] += fw * (r[cc] * ev[cc]).sum()
                W[side, k] += fw * r[cc].sum()
    pot = info["pot"]
    Vc = np.where(W > 0, V / np.maximum(W, 1e-12), np.nan)
    EV = ev_side / wsum_side
    rho = 1.0 - EV.sum() / pot
    # class reach (combos weight per class / class combo count) for each side
    reach = W / fw_tot / np.array(NCOMBOS)[None, :]
    share = Vc / pot                                    # realised share of the flop pot
    # direct model: share(h) = (1 - rho) R(h) e(h), e = equity vs the opponent's flop range
    R = np.ones((2, 169))
    net = 1.0 - rho
    eqs = []
    for i in range(2):
        j = 1 - i
        eq = (P * E * reach[j][None, :]).sum(1) / np.maximum((P * reach[j][None, :]).sum(1), 1e-12)
        eqs.append(eq)
        ok = np.isfinite(share[i]) & (reach[i] > 1e-6) & (eq > 1e-6)
        R[i][ok] = share[i][ok] / (net * eq[ok])
    err = []
    out = {"pot": pot, "stack": info["stack"], "spr": info["spr"], "rho": rho, "flops": len(files),
           "max_expl_pct": max(expl), "median_expl_pct": float(np.median(expl)), "sides": {}}
    for i, seat in ((0, info["oop"]), (1, info["ip"])):
        j = 1 - i
        m = net * R[i] * eqs[i]
        ok = np.isfinite(share[i]) & (reach[i] > 1e-6)
        wts = reach[i][ok] * np.array(NCOMBOS)[ok]
        eq = eqs[i]
        # error of the bucketed model is computed below; buckets by reach-weighted quintiles
        order = np.argsort(eq)
        cw = np.cumsum((reach[i] * np.array(NCOMBOS))[order])
        cw /= cw[-1]
        edges = [float(eq[order][np.searchsorted(cw, q)]) for q in np.linspace(0, 1, NB + 1)[1:-1]]
        b = np.searchsorted(edges, eq)
        Rb = []
        for k in range(NB):
            sel = ok & (b == k)
            ww = reach[i][sel] * np.array(NCOMBOS)[sel]
            Rb.append(float((R[i][sel] * ww).sum() / ww.sum()) if ww.sum() > 0 else float("nan"))
        Rcls = np.array([Rb[k] if np.isfinite(Rb[k]) else np.nanmean(Rb) for k in b])
        # per-class R (playability: suited connectors, small pairs, suited aces), shrunk toward
        # the bucket value with a prior weight of K_SHRINK combos
        wc = reach[i] * np.array(NCOMBOS)
        Rraw = np.where(ok, R[i], Rcls)
        Rclass = (wc * Rraw + K_SHRINK * Rcls) / (wc + K_SHRINK)
        err.append(float(np.sqrt((wts * (net * Rcls[ok] * eq[ok] - share[i][ok]) ** 2).sum() / wts.sum())))
        aggr = info["aggressor"] == seat
        out["sides"][seat] = {
            "ip": int(i == 1), "aggressor": int(aggr), "edges": edges, "R": Rb,
            "R_class": [float(x) for x in Rclass],
            "fit_rmse_share_class": float(np.sqrt((wts * (net * Rclass[ok] * eq[ok] - share[i][ok]) ** 2).sum() / wts.sum())),
            "R_mean": float((R[i][ok] * wts).sum() / wts.sum()),
            "fit_rmse_share": err[-1],
            "ev_bb": float(EV[i]), "eq_mean": float((eq[ok] * wts).sum() / wts.sum()),
            "realised_share": float(EV[i] / pot),
        }
    return out


def main():
    rdir, sdir, outp = sys.argv[1], sys.argv[2], sys.argv[3]
    spots = json.load(open(os.path.join(rdir, "spots.json")))
    flops = json.load(open("flops.json"))["flops"]
    E, P = load_eq()
    res = {}
    for name, info in spots.items():
        if info["oop_combos"] < 1 or info["ip_combos"] < 1:
            print(f"{name}: skipped (a range has under 1 combo: {info['oop_combos']:.2f} / {info['ip_combos']:.2f})")
            continue
        r = fit_spot(name, info, sdir, flops, E, P)
        if r is None:
            continue
        res[name] = r
        so = r["sides"][info["oop"]]
        si = r["sides"][info["ip"]]
        print(f"{name:16s} flops {r['flops']:3d} rho {r['rho']:.3f} (rake {r['rho'] * r['pot']:.2f}bb) "
              f"OOP {info['oop']} R {so['R_mean']:.3f} share {so['realised_share']:.3f} "
              f"IP {info['ip']} R {si['R_mean']:.3f} share {si['realised_share']:.3f} "
              f"| buckets OOP {['%.2f' % x for x in so['R']]} IP {['%.2f' % x for x in si['R']]} "
              f"| fit rmse {so['fit_rmse_share']:.3f}/{si['fit_rmse_share']:.3f} maxexpl {r['max_expl_pct']:.2f}%")
    json.dump(res, open(outp, "w"), indent=1)


if __name__ == "__main__":
    main()
