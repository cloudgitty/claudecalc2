"""Hand-structure features for all classes (vectorised numpy)."""
import numpy as np
from common import decode


def features(codes):
    r, s = decode(codes)
    n = len(codes)
    r = r.astype(np.int64); s = s.astype(np.int64)
    F = {}
    cnt = np.zeros((n, 13), np.int64)
    for i in range(6): np.add.at(cnt, (np.arange(n), r[:, i]), 1)
    for k in range(13): F[f"cnt{k}"] = cnt[:, k]
    sc = np.zeros((n, 4), np.int64)
    for i in range(6): np.add.at(sc, (np.arange(n), s[:, i]), 1)
    scs = -np.sort(-sc, axis=1)
    for k in range(4): F[f"suitc{k}"] = scs[:, k]
    F["suit_pattern"] = scs[:, 0] * 1000 + scs[:, 1] * 100 + scs[:, 2] * 10 + scs[:, 3]
    # per-suit top/second rank (for suits with >=2 cards), ordered by suit size then top rank
    top = np.full((n, 4), -1); sec = np.full((n, 4), -1)
    for su in range(4):
        m = (s == su)
        rr = np.where(m, r, -1)
        srt = -np.sort(-rr, axis=1)
        has2 = sc[:, su] >= 2
        top[:, su] = np.where(has2, srt[:, 0], -1)
        sec[:, su] = np.where(has2, srt[:, 1], -1)
    key = sc * 100 + top
    o = np.argsort(-key, axis=1)
    topo = np.take_along_axis(top, o, 1); seco = np.take_along_axis(sec, o, 1)
    sco = np.take_along_axis(sc, o, 1)
    for k in range(3):
        F[f"stop{k}"] = topo[:, k]; F[f"ssec{k}"] = seco[:, k]; F[f"ssz{k}"] = sco[:, k]
    F["n_suited2"] = (sc >= 2).sum(1); F["n_suited3"] = (sc >= 3).sum(1); F["n_suited4"] = (sc >= 4).sum(1)
    F["nfd"] = (top == 12).sum(1)                       # suits holding the ace with >=2 cards
    F["kfd"] = (top == 11).sum(1)
    F["qfd"] = (top == 10).sum(1)
    F["flush_quality"] = np.where(top >= 0, top, 0).sum(1)
    # pairs
    F["n_pair"] = (cnt == 2).sum(1); F["n_trip"] = (cnt == 3).sum(1); F["n_quad"] = (cnt == 4).sum(1)
    rk = np.arange(13)[None, :]
    paired = np.where(cnt >= 2, rk, -1)
    ps = -np.sort(-paired, axis=1)
    F["pair1"] = ps[:, 0]; F["pair2"] = ps[:, 1]; F["pair3"] = ps[:, 2]
    F["trip_rank"] = np.where(cnt >= 3, rk, -1).max(1)
    # distinct ranks, connectivity
    pres = (cnt > 0).astype(np.int64)
    ext = np.concatenate([pres[:, 12:13], pres], axis=1)    # ace low at index 0
    win = np.stack([ext[:, j:j + 5].sum(1) for j in range(10)], axis=1)   # windows A-5 .. T-A
    F["n_distinct"] = pres.sum(1)
    F["win_max"] = win.max(1)
    for t in (2, 3, 4, 5): F[f"win_ge{t}"] = (win >= t).sum(1)
    F["win_top4"] = (win[:, 5:] >= 4).sum(1)       # high windows (8-high and above) with 4 ranks
    F["win_top3"] = (win[:, 5:] >= 3).sum(1)
    hiwin = np.where(win >= 3, np.arange(10)[None, :], -1).max(1)
    F["best3win"] = hiwin
    hiwin4 = np.where(win >= 4, np.arange(10)[None, :], -1).max(1)
    F["best4win"] = hiwin4
    # straight-capable rank pairs (distinct ranks within a span of 4, ace counts low too)
    dr = np.where(pres > 0, rk, -99)
    cap = np.zeros(n, np.int64); capw = np.zeros(n, np.float64)
    for a in range(13):
        for b in range(a + 1, 13):
            d = b - a
            ok = d <= 4 or (b == 12 and a <= 3)
            if not ok: continue
            both = (pres[:, a] > 0) & (pres[:, b] > 0)
            cap += both; capw += both * (b + a)
    F["str_pairs"] = cap; F["str_pairs_w"] = capw
    # runs of consecutive distinct ranks
    run = np.zeros(n, np.int64); cur = np.zeros(n, np.int64)
    for k in range(14):
        col = ext[:, k] if k < 14 else 0
        cur = np.where(col > 0, cur + 1, 0); run = np.maximum(run, cur)
    F["longest_run"] = run
    # gaps between distinct ranks
    srt = -np.sort(-np.where(pres > 0, rk, -1), axis=1)
    F["span"] = srt[:, 0] - np.where(srt >= 0, srt, 99).min(1)
    # danglers: unpaired, unsuited, no distinct rank within 4 (ace low wraps)
    dang = np.zeros(n, np.int64)
    for i in range(6):
        ri = r[:, i]; si = s[:, i]
        unp = np.take_along_axis(cnt, ri[:, None], 1)[:, 0] == 1
        uns = np.take_along_axis(sc, si[:, None], 1)[:, 0] == 1
        near = np.zeros(n, bool)
        for j in range(6):
            if j == i: continue
            rj = r[:, j]
            dd = np.abs(ri - rj)
            al = ((ri == 12) & (rj <= 3)) | ((rj == 12) & (ri <= 3))
            near |= ((dd >= 1) & (dd <= 4)) | al
        dang += unp & uns & ~near
    F["danglers"] = dang
    F["rank_sum"] = r.sum(1)
    F["n_broadway"] = (r >= 8).sum(1)
    F["n_low"] = (r <= 3).sum(1)
    for i in range(6): F[f"r{i}"] = r[:, i]
    names = list(F)
    X = np.stack([F[k] for k in names], axis=1).astype(np.float32)
    return X, names
