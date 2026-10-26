"""Stage 4: measure R0 (equity realisation) with full flop-turn-river simulations
under a simple fixed strategy, identical for both seats, by equity bucket: each
street a hand's equity vs the opponent's preflop range is turned into a percentile
within its own range on that board; bet pot with the top 30% (first actor traps half
of its top 10%) and the bottom 12% (bluffs), raise pot with the top 10%, defend the
minimum-defence frequency of the current range against a bet, call a raise with the
top 20%.  (A first attempt with absolute equity thresholds vs the unconditioned range
gave R_oop ~0.0-0.4 at SPR 30, an artefact of value-only betting; see REPORT.)"""
import json, time
import numpy as np
import engine
from common import W, progress, classes

VAL, BLF, RAI, RCALL, MQ, K, M2, NS = 0.70, 0.12, 0.90, 0.80, 32, 32, 16, 40_000


def pool_from(sel, seed):
    codes, combos = classes()
    w = np.where(sel, combos, 0).astype(float)
    idx = np.random.default_rng(seed).choice(len(codes), 300_000, p=w / w.sum())
    return engine.sample_reps(codes[idx], seed + 1)


def main():
    pct = np.load(W("bands.npz"))["pct"].astype(float)
    # (label, pot type, OOP range, IP range, pot at flop, stack behind) -- starting-point ranges
    cfg = [
        ("SRP BB call vs BTN open", "srp", (pct > 6) & (pct <= 50), pct <= 48, 6.5, 197.0),
        ("SRP BB call vs CO open", "srp", (pct > 6) & (pct <= 40), pct <= 32, 6.5, 197.0),
        ("3BP BB 3bet vs BTN call", "3bp", pct <= 6, (pct > 6) & (pct <= 30), 24.5, 188.0),
        ("3BP CO call vs BTN 3bet", "3bp", (pct > 5) & (pct <= 22), pct <= 5, 22.5, 189.5),
        ("4BP BB call vs BTN 4bet", "4bp", (pct > 2.5) & (pct <= 6), pct <= 2.5, 73.5, 163.5),
        ("4BP CO 4bet vs BTN call", "4bp", pct <= 2.5, (pct > 2.5) & (pct <= 5), 67.5, 167.0),
    ]
    res = {}; acc = {}
    for i, (lab, pt, oop, ip, pot, stk) in enumerate(cfg):
        t0 = time.time()
        A = pool_from(oop, 10 * i + 1); B = pool_from(ip, 10 * i + 5)
        so, eo, si, ei = engine.sim_postflop_p(NS, A, B, pot, stk, MQ, K, M2, VAL, BLF, RAI, RCALL, 1000 + i)
        r_o, r_i = so.sum() / eo.sum(), si.sum() / ei.sum()
        se_o = np.std(so) / np.sqrt(NS) / eo.mean(); se_i = np.std(si) / np.sqrt(NS) / ei.mean()
        res[lab] = dict(pot_type=pt, R_oop=r_o, R_ip=r_i, se_oop=se_o, se_ip=se_i,
                        eq_oop=eo.mean(), eq_ip=ei.mean(), samples=NS)
        a = acc.setdefault(pt, [[], []]); a[0].append(r_o); a[1].append(r_i)
        print(f"{lab}: R_oop {r_o:.3f}+-{se_o:.3f} R_ip {r_i:.3f}+-{se_i:.3f} ({time.time()-t0:.0f}s)", flush=True)
    R0 = {}
    for pt, (o, i) in acc.items():
        R0[f"{pt},oop"] = float(np.mean(o)); R0[f"{pt},ip"] = float(np.mean(i))
    R0["srp,sbcold"] = R0["srp,oop"] * 0.72 / 0.82      # BB-behind penalty kept from the default ratio
    json.dump(dict(R0=R0, runs=res, strategy=dict(val=VAL, bluff=BLF, raise_=RAI, raise_call=RCALL,
                                                  mq=MQ, K=K, m2=M2, samples=NS)), open(W("r0.json"), "w"), indent=1)
    progress("(4) R0 calibrated: " + ", ".join(f"{k}={v:.3f}" for k, v in R0.items()))


if __name__ == "__main__":
    main()
