"""Stage 8a: sensitivity reruns, hand-level sanity, first-principles equity checks."""
import json, pickle, sys, time
import numpy as np
import engine
from common import W, progress, classes
from solve import Solver, SPOT_ORDER, OUTPUT
from stage5_iterate import load_r0
from stage1_classes import class_code_ref

RS = "shdc"; RK = "23456789TJQKA"


def card(s):
    return RK.index(s[0]) * 4 + RS.index(s[1])


HANDS = [
    ("AAKK ds + QJ", "As Ah Ks Kh Qd Jc"), ("AAKK ds + 72", "As Ah Ks Kh 7d 2c"),
    ("AAJT ds + 98", "As Ah Js Th 9d 8c"), ("AAJT ds + 52", "As Ah Js Th 5d 2c"),
    ("AKQJT9 ds", "As Ks Qh Jh Td 9c"), ("KQJT98 ds", "Ks Qs Jh Th 9d 8c"),
    ("A2345K ds", "As 2s 3h 4h 5d Kc"), ("A2345x ds (x=9)", "As 2s 3h 4h 5d 9c"),
    ("222 + 789 ds", "2s 2h 2d 7s 8h 9c"), ("223344 ds", "2s 2h 3s 3h 4d 4c"),
    ("2233 + AK ds", "2s 2h 3d 3c As Kh"), ("444 + AKQ", "4s 4h 4d As Kh Qc"),
    ("unconnected K9632 + 8? (2-2-1-1)", "Kc 8d 3h Js 5c 2h"), ("unconnected Q9742+K", "Qs 9h 7d 4c 2s Kh"),
    ("single suit AKQJT9", "As Ks Qs Js Ts 9s"), ("single suit Q T 8 6 4 2", "Qs Ts 8s 6s 4s 2s"),
    ("AAA + KQJ", "As Ah Ad Kc Qs Jh"), ("AAA + 742", "As Ah Ad 7c 4s 2h"),
]


def hand_sanity(state):
    codes, _ = classes()
    st = np.load(state)
    out = []
    for lab, hs in HANDS:
        cards = [card(x) for x in hs.split()]
        code = class_code_ref([(c // 4, c % 4) for c in cards])
        cid = int(np.searchsorted(codes, code))
        row = {}
        for name in SPOT_ORDER:
            node, o, d = OUTPUT[name]
            if node == "O1": c, r = 0.0, st[f"open_{o}"][cid]
            else:
                key = f"{o}-{d}"
                if node == "D1": c, r = st[f"{key}_D1c"][cid], st[f"{key}_D1r"][cid]
                elif node == "O2":
                    ok = st[f"open_{o}"][cid] > 0
                    c, r = (st[f"{key}_O2c"][cid], st[f"{key}_O2r"][cid]) if ok else (0, 0)
                else:
                    ok = st[f"{key}_D1r"][cid] > 0
                    c, r = (st[f"{key}_D2c"][cid], st[f"{key}_D2r"][cid]) if ok else (0, 0)
            row[name] = (int(round(100 * c)), int(round(100 * r)))
        out.append(dict(label=lab, cards=hs, class_id=cid, freqs=row))
    return out


def first_principles():
    """Exact identities and exact enumeration (nothing quoted from literature)."""
    codes, combos = classes()
    w = combos.astype(float)
    res = {}
    band = np.load(W("bands.npz"))["band"]
    E = np.stack([np.load(W(f"eqm_band{b:02d}.npz"))["eq"] for b in range(12)], 1).astype(float)
    Er = np.stack([np.load(W(f"eqm_band{b:02d}.npz"))["raw"] for b in range(12)], 1).astype(float)
    share = np.bincount(band, weights=w, minlength=12) / w.sum()
    # identity 1: mean equity of a random hand vs a random hand is exactly 0.5
    res["mean_eq_random_vs_random_raw"] = float(np.average(Er @ share, weights=w))
    res["mean_eq_random_vs_random_posterior"] = float(np.average(E @ share, weights=w))
    # identity 2: band a vs band b + band b vs band a = 1 (card removal makes it exact only on average)
    M = np.zeros((12, 12))
    for a in range(12):
        m = band == a
        M[a] = np.average(Er[m], weights=w[m], axis=0)
    res["max_|eq(a,b)+eq(b,a)-1|"] = float(np.abs(M + M.T - 1).max())
    res["band_matrix_diag"] = [round(float(x), 4) for x in np.diag(M)]
    # exact enumeration vs Monte Carlo for fixed matchups
    ex = []
    for hs, vs in [("As Ah Ks Kh Qd Jc", "Ts 9h 8s 7h 6d 5c"), ("As Ks Qh Jh Td 9c", "Ac Ad 7s 6h 3d 2c"),
                   ("2s 2h 2d 7s 8h 9c", "Kc Qc Jd Td 9s 8h")]:
        h = [card(x) for x in hs.split()]; v = [card(x) for x in vs.split()]
        e = engine.exact_hvh(h, v); m = engine.mc_hvh(h, v, 400000, 5)
        ex.append(dict(hero=hs, villain=vs, exact=e, mc_400k=m, z=(m - e) / np.sqrt(e * (1 - e) / 400000)))
    res["exact_vs_mc"] = ex
    return res


def sensitivity(state, corr):
    base = Solver(R0=load_r0(), log=lambda *a: None); base.load(state); wb = base.widths()
    runs = {"R0+0.05": dict(R0=load_r0(0.05)), "R0-0.05": dict(R0=load_r0(-0.05)),
            "open 3.5bb": dict(R0=load_r0(), open_size=3.5), "open 2.5bb": dict(R0=load_r0(), open_size=2.5)}
    out = {}
    for lab, kw in runs.items():
        t0 = time.time()
        s = Solver(log=progress, **kw); s.load(state); s.corr = dict(corr)
        hist, conv = s.iterate(10, tag=f"sens {lab}")
        wn = s.widths()
        out[lab] = dict(converged=conv, rounds=len(hist),
                        delta={k: (round(wn[k][0] - wb[k][0], 2), round(wn[k][1] - wb[k][1], 2)) for k in SPOT_ORDER},
                        widths={k: (round(wn[k][0], 2), round(wn[k][1], 2)) for k in SPOT_ORDER})
        mx = max(max(abs(a), abs(b)) for a, b in out[lab]["delta"].values())
        progress(f"(8) sensitivity {lab}: converged={conv} in {len(hist)} rounds, max width move {mx:.2f} pts ({(time.time()-t0)/60:.0f} min)")
    return out


def main():
    state = W("state_final.npz")
    corr = pickle.load(open(W("corr.pkl"), "rb"))
    res = {"hands": hand_sanity(state), "first_principles": first_principles()}
    json.dump(res, open(W("checks.json"), "w"), indent=1)
    progress(f"(8) first-principles checks: {json.dumps(res['first_principles'])[:600]}")
    res["sensitivity"] = sensitivity(state, corr)
    json.dump(res, open(W("checks.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
