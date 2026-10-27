"""Stage 6: re-simulate every class in the borderline band of every cutoff of the
final iteration directly against the actual villain range (exact card removal)
until SE <= 0.3 equity points; store the corrections (exact - band mixture),
then run refinement rounds with the corrections in place."""
import json, os, sys, time
import numpy as np
import engine
from common import W, progress, classes
from solve import Solver, OUTPUT, SPOT_ORDER, SEATS
from stage5_iterate import load_r0

SE_TARGET = 0.003
BUDGET_TRIALS = 4.3e10          # ~60 min at 12M trials/s


def band_idx(margin, w, half):
    sel = np.nonzero(w > 0)[0]
    if len(sel) == 0: return sel
    m = margin[sel]; ww = w[sel]
    o = np.argsort(m, kind="stable"); cw = np.cumsum(ww[o]); tot = cw[-1]
    q = (cw - ww[o] / 2) / tot; q0 = ww[m < 0].sum() / tot
    return sel[o[np.abs(q - q0) <= half]]


def collect(s, half, half_rfi):
    s.sweep()                                     # fills s.aux / s.v_open for the current strategies
    tasks = {}
    def add(rk, idx):
        tasks.setdefault(rk, set()).update(idx.tolist())
    bands = {}
    for name in SPOT_ORDER:
        node, o, d = OUTPUT[name]
        if node == "O1":
            idx = band_idx(s.v_open[o], s.w, half_rfi)
            for dd in SEATS[SEATS.index(o) + 1:]:
                add(f"dcall:{o}-{dd}", idx); add(f"d3:{o}-{dd}", idx)
            bands[name] = len(idx); continue
        key = f"{o}-{d}"
        vc, vr, reach = s.aux[key][node]
        ir = band_idx(vr - np.maximum(vc, 0), reach, half)
        ic = band_idx(vc, reach, half)
        bands[name] = len(set(ir.tolist()) | set(ic.tolist()))
        if node == "D1":
            add(f"open:{key}", ic)
            for k in ("ocall3", "o4", "ocalljam"): add(f"{k}:{key}", ir)
        elif node == "O2":
            add(f"d3:{key}", ic); add(f"d3:{key}", ir)
            for k in ("dcall4", "djam"): add(f"{k}:{key}", ir)
        elif node == "D2":
            add(f"o4:{key}", ic); add(f"o4:{key}", ir); add(f"ocalljam:{key}", ir)
    return tasks, bands


def main():
    t0 = time.time()
    codes, combos = classes()
    s = Solver(R0=load_r0(), log=progress)
    s.load(W("state_main.npz"))
    half, half_rfi = 0.02, 0.015
    while True:
        tasks, bands = collect(s, half, half_rfi)
        ntask = sum(len(v) for v in tasks.values())
        est = ntask * 0.22 / SE_TARGET ** 2
        if est <= BUDGET_TRIALS or half <= 0.0101: break
        progress(f"(6) borderline band +-{100*half:.1f}% => {ntask} tasks, est {est:.2e} trials > budget; narrowing")
        half -= 0.0025; half_rfi = min(half_rfi, half)
    progress(f"(6) borderline band +-{100*half:.2f}% of reaching combos (RFI +-{100*half_rfi:.2f}%): "
             f"{ntask} (class, range) tasks over {len(tasks)} ranges; classes per spot: {json.dumps(bands)}")
    # pools of the actual ranges
    rkeys = sorted(tasks)
    pools = []
    for i, rk in enumerate(rkeys):
        kind, key = rk.split(":")
        V = s.ranges(s.pairs[key])[kind]
        if V.sum() <= 0: V = s.w
        idx = np.random.default_rng(1000 + i).choice(s.n, 400_000, p=V / V.sum())
        pools.append(engine.sample_reps(codes[idx], 2000 + i))
    P = engine.Pools(pools)
    hero = np.concatenate([np.array(sorted(tasks[rk]), dtype=np.int64) for rk in rkeys])
    pid = np.concatenate([np.full(len(tasks[rk]), i, dtype=np.int32) for i, rk in enumerate(rkeys)])
    n1 = 12000
    tt = time.time()
    S1 = engine.sim_tasks(codes[hero], pid, np.full(len(hero), n1), P, 777)
    rate = len(hero) * n1 / (time.time() - tt)
    c1 = np.maximum(S1[4], 1)
    eq = S1[0] / c1; var = np.maximum(S1[1] / c1 - eq ** 2, 1e-4)
    need = np.ceil(var / SE_TARGET ** 2).astype(np.int64) - S1[4].astype(np.int64)
    more = np.nonzero((need > 0) & (S1[4] > 0))[0]
    progress(f"(6) pass 1: {len(hero)} tasks x {n1} trials at {rate/1e6:.1f}M trials/s; {len(more)} need a top-up; "
             f"{int((S1[4] < n1).sum())} tasks lost trials to card collisions")
    tot = [x.copy() for x in S1]
    if len(more):
        S2 = engine.sim_tasks(codes[hero[more]], pid[more], need[more].astype(np.int32), P, 778)
        for k in range(5): tot[k][more] += S2[k]
    ntr = tot[4]
    valid = ntr > 0
    ntr1 = np.maximum(ntr, 1)
    eq = tot[0] / ntr1; var = np.maximum(tot[1] / ntr1 - eq ** 2, 1e-6); se = np.where(valid, np.sqrt(var / ntr1), 0)
    pl = (tot[2] - tot[3]) / ntr1
    hero, pid, eq, se, pl, ntr = hero[valid], pid[valid], eq[valid], se[valid], pl[valid], ntr[valid]
    np.savez(W("resim.npz"), hero=hero, pid=pid, eq=eq, se=se, pl=pl, ntr=ntr, rkeys=np.array(rkeys))
    # corrections relative to the band mixture, and validation of the mixture model
    errs = []
    for i, rk in enumerate(rkeys):
        m = pid == i
        kind, key = rk.split(":")
        V = s.ranges(s.pairs[key])[kind]
        s.corr.pop(rk, None)
        em, pm = s.eqv(rk, V)
        idx = hero[m]
        s.corr[rk] = (idx, eq[m] - em[idx], pl[m] - pm[idx])
        errs.append(eq[m] - em[idx])
    errs = np.concatenate(errs)
    stats = dict(tasks=int(len(hero)), max_se=float(se.max()), mean_se=float(se.mean()), trials=int(ntr.sum()),
                 mixture_bias=float(errs.mean()), mixture_rmse=float(np.sqrt((errs ** 2).mean())),
                 mixture_p95_abs=float(np.percentile(np.abs(errs), 95)), half=half, half_rfi=half_rfi, bands=bands)
    json.dump(stats, open(W("resim_stats.json"), "w"), indent=1)
    progress(f"(6) re-sim done: max SE {100*se.max():.3f} pts, mean SE {100*se.mean():.3f}; band-mixture vs exact: "
             f"bias {100*errs.mean():+.2f} pts, RMSE {100*stats['mixture_rmse']:.2f}, p95 {100*stats['mixture_p95_abs']:.2f} "
             f"({ntr.sum():.2e} trials, {(time.time()-t0)/60:.0f} min)")
    import pickle
    pickle.dump(s.corr, open(W("corr.pkl"), "wb"))
    hist, conv = s.iterate(6, tag="refine")
    s.save(W("state_final.npz"))
    json.dump(hist, open(W("hist_refine.json"), "w"))
    progress(f"(6) refinement rounds with exact corrections: converged={conv} after {len(hist)} rounds")


if __name__ == "__main__":
    main()
