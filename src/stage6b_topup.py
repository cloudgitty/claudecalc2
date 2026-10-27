"""Stage 6b: top up every borderline re-simulation whose SE is still above 0.3 pts
(stage 6 stopped at an expected SE of exactly 0.3, so about half ended slightly
above), then redo the corrections and the refinement rounds.  The pools are rebuilt
from the same state and seeds as in stage 6, so they are identical."""
import json, pickle, time
import numpy as np
import engine
from common import W, progress, classes
from solve import Solver
from stage5_iterate import load_r0

TARGET = 0.0029        # aim a little under 0.3 pts so the realised SE lands under it


def main():
    t0 = time.time()
    codes, _ = classes()
    s = Solver(R0=load_r0(), log=progress)
    s.load(W("state_main.npz"))
    d = dict(np.load(W("resim.npz")))
    rkeys = list(d["rkeys"])
    pools = []
    for i, rk in enumerate(rkeys):
        kind, key = rk.split(":")
        V = s.ranges(s.pairs[key])[kind]
        if V.sum() <= 0: V = s.w
        idx = np.random.default_rng(1000 + i).choice(s.n, 400_000, p=V / V.sum())
        pools.append(engine.sample_reps(codes[idx], 2000 + i))
    P = engine.Pools(pools)
    hero, pid, eq, se, pl, ntr = d["hero"], d["pid"], d["eq"], d["se"], d["pl"], d["ntr"].astype(float)
    var = se ** 2 * ntr
    s1 = eq * ntr; s2 = (var + eq ** 2) * ntr; spl = pl * ntr
    degenerate = ntr < 6000          # range almost always collides with the hero's cards
    for rnd in range(3):
        need = np.ceil(np.maximum(var, 1e-4) / TARGET ** 2) - ntr
        todo = np.nonzero((se > 0.003) & ~degenerate & (need > 0))[0]
        if len(todo) == 0: break
        S = engine.sim_tasks(codes[hero[todo]], pid[todo], need[todo].astype(np.int32), P, 779 + rnd)
        s1[todo] += S[0]; s2[todo] += S[1]; spl[todo] += S[2] - S[3]; ntr[todo] += S[4]
        eq = s1 / ntr; var = np.maximum(s2 / ntr - eq ** 2, 1e-6); se = np.sqrt(var / ntr); pl = spl / ntr
        progress(f"(6b) top-up pass {rnd+1}: {len(todo)} tasks, {need[todo].sum():.2e} trials; "
                 f"share with SE <= 0.3 pts now {(se[~degenerate] <= 0.003).mean():.4f}")
    np.savez(W("resim.npz"), hero=hero, pid=pid, eq=eq, se=se, pl=pl, ntr=ntr, rkeys=np.array(rkeys))
    errs = []
    s.corr = {}
    for i, rk in enumerate(rkeys):
        m = (pid == i) & ~degenerate
        kind, key = rk.split(":")
        em, pm = s.eqv(rk, s.ranges(s.pairs[key])[kind])
        idx = hero[m]
        s.corr[rk] = (idx, eq[m] - em[idx], pl[m] - pm[idx])
        errs.append(eq[m] - em[idx])
    errs = np.concatenate(errs)
    st = json.load(open(W("resim_stats.json")))
    ok = ~degenerate
    st.update(tasks=int(ok.sum()), degenerate_tasks=int(degenerate.sum()), trials=int(ntr.sum()),
              max_se=float(se[ok].max()), mean_se=float(se[ok].mean()), share_se_le_0p3=float((se[ok] <= 0.003).mean()),
              mixture_bias=float(errs.mean()), mixture_rmse=float(np.sqrt((errs ** 2).mean())),
              mixture_p95_abs=float(np.percentile(np.abs(errs), 95)))
    json.dump(st, open(W("resim_stats.json"), "w"), indent=1)
    pickle.dump(s.corr, open(W("corr.pkl"), "wb"))
    progress(f"(6b) re-sim final: {ok.sum()} tasks, max SE {100*st['max_se']:.3f} pts, share SE<=0.3 {st['share_se_le_0p3']:.4f}; "
             f"{degenerate.sum()} degenerate tasks dropped (range collides with the hero's cards); mixture bias "
             f"{100*st['mixture_bias']:+.2f} pts, RMSE {100*st['mixture_rmse']:.2f} ({(time.time()-t0)/60:.0f} min)")
    hist, conv = s.iterate(6, tag="refine")
    s.save(W("state_final.npz"))
    json.dump(hist, open(W("hist_refine.json"), "w"))
    progress(f"(6b) refinement rounds with exact corrections: converged={conv} after {len(hist)} rounds")


if __name__ == "__main__":
    main()
