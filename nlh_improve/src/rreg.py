"""R regression for heads-up flop terminals that were not solved (improvement plan item 4).

Observations: the per-class R of every solved spot and side (fit.json "R_class"), weighted by the
class's combos in range. Features of a (terminal, side, hand class):
  constant, in position, preflop aggressor, log SPR,
  strength bucket (equity vs the opponent's range, quintiles of the side's own range; 4 dummies),
  pair, suited, connected (gap <= 1, not a pair), suited ace, broadway (both cards T+).
Weighted least squares with a small ridge penalty. `cross_validate` refits leaving one spot out
and reports the weighted RMSE of R for this model and for the strength-bucket-only model.

usage: python3 src/rreg.py <fit.json> <ranges_dir>      (cross-validation report)
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from hands import NCOMBOS  # noqa: E402

NB = 5
RIDGE = 1e-3
RANKS = "AKQJT98765432"


def class_features():
    """pair, suited, connected, suited ace, broadway for the 169 grid classes."""
    F = np.zeros((169, 5))
    for k in range(169):
        i, j = divmod(k, 13)
        hi, lo = min(i, j), max(i, j)      # 0 = ace
        pair = i == j
        suited = i < j
        gap = lo - hi
        conn = (not pair) and (gap <= 2 or (hi == 0 and lo >= 10))   # adjacent or one gap; A2-A4 wheel
        F[k] = [pair, suited, conn, suited and hi == 0, hi <= 4 and lo <= 4]
    return F


CF = class_features()


def bucket_of(eq, w):
    """Strength bucket (0..NB-1) per class: quintiles of the reach-weighted equity distribution."""
    order = np.argsort(eq)
    cw = np.cumsum(w[order])
    cw = cw / cw[-1] if cw[-1] > 0 else np.linspace(0, 1, 169)
    edges = [float(eq[order][min(np.searchsorted(cw, q), 168)]) for q in np.linspace(0, 1, NB + 1)[1:-1]]
    return np.searchsorted(edges, eq)


def design(ip, aggr, spr, bucket, with_hand=True):
    n = len(bucket)
    cols = [np.ones(n), np.full(n, ip), np.full(n, aggr), np.full(n, np.log(max(spr, 0.1)))]
    for b in range(1, NB):
        cols.append((bucket == b).astype(float))
    X = np.stack(cols, 1)
    if with_hand:
        X = np.concatenate([X, CF], 1)
    return X


def observations(fit, eqs=None):
    """Rows (X, y, w, spot) from fit.json. eqs: {spot: {seat: equity array}} for the buckets; when
    absent the bucket comes from the stored edges and the stored per-class equities if present."""
    rows = []
    for name, f in fit.items():
        for seat, sd in f["sides"].items():
            if "R_class" not in sd or "eq_class" not in sd:
                continue
            eq = np.array(sd["eq_class"])
            w = np.array(sd["w_class"])
            b = np.searchsorted(sd["edges"], eq)
            rows.append((name, sd["ip"], sd["aggressor"], f["spr"], b, np.array(sd["R_class"]), w))
    return rows


def fit_model(rows, with_hand=True):
    X = np.concatenate([design(ip, ag, spr, b, with_hand) for _, ip, ag, spr, b, _, _ in rows])
    y = np.concatenate([r for *_, r, _ in rows])
    w = np.concatenate([w for *_, w in rows])
    ok = w > 0
    Xw = X[ok] * np.sqrt(w[ok])[:, None]
    yw = y[ok] * np.sqrt(w[ok])
    A = Xw.T @ Xw + RIDGE * np.eye(X.shape[1]) * w[ok].sum()
    return np.linalg.solve(A, Xw.T @ yw)


def predict(beta, ip, aggr, spr, bucket, with_hand=True):
    return design(ip, aggr, spr, bucket, with_hand) @ beta


def cross_validate(rows):
    spots = sorted({r[0] for r in rows})
    err = {True: [0.0, 0.0], False: [0.0, 0.0]}
    for s in spots:
        train = [r for r in rows if r[0] != s]
        test = [r for r in rows if r[0] == s]
        for wh in (True, False):
            beta = fit_model(train, wh)
            for _, ip, ag, spr, b, y, w in test:
                p = predict(beta, ip, ag, spr, b, wh)
                err[wh][0] += float((w * (p - y) ** 2).sum())
                err[wh][1] += float(w.sum())
    return {("with hand types" if k else "buckets only"): (v[0] / v[1]) ** 0.5 for k, v in err.items()}


def main():
    fit = json.load(open(sys.argv[1]))
    rows = observations(fit)
    if not rows:
        raise SystemExit("fit.json has no per-class equities (refit with the current src/fit_r.py)")
    cv = cross_validate(rows)
    beta = fit_model(rows, True)
    names = ["const", "ip", "aggressor", "log spr"] + [f"bucket {b}" for b in range(1, NB)] + \
            ["pair", "suited", "connected", "suited ace", "broadway"]
    print("leave-one-spot-out weighted RMSE of R:", {k: round(v, 4) for k, v in cv.items()})
    print("coefficients:", {n: round(float(c), 3) for n, c in zip(names, beta)})


if __name__ == "__main__":
    main()
