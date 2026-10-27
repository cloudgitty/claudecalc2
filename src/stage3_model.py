"""Stage 3: feature model of equity (and playability) vs each reference band,
5-fold cross-fitted (each fold = 20% holdout), then empirical-Bayes shrinkage
of every simulated class toward its out-of-fold model prediction."""
import os, sys, time, json
import numpy as np
import lightgbm as lgb
from scipy.stats import spearmanr
from common import W, progress, classes

PARAMS = dict(objective="regression", learning_rate=0.15, num_leaves=255, min_data_in_leaf=40,
              feature_fraction=0.9, bagging_fraction=0.8, bagging_freq=1, lambda_l2=1.0,
              verbose=-1, num_threads=4)
ROUNDS = int(os.environ.get("ROUNDS", 300))
NB = 12


def crossfit(X, y, w, seed, rounds=ROUNDS):
    n = len(y)
    fold = np.random.default_rng(seed).integers(0, 5, n)
    oof = np.zeros(n)
    for k in range(5):
        tr = fold != k
        m = lgb.train(PARAMS, lgb.Dataset(X[tr], y[tr], weight=w[tr]), rounds)
        oof[~tr] = m.predict(X[~tr], num_threads=4)
    return oof, fold


def stats(y, pred, noise_var, w, fold):
    out = []
    for k in range(5):
        h = fold == k
        yy, pp, ww, nv = y[h], pred[h], w[h], noise_var[h]
        mu = np.average(yy, weights=ww)
        sst = np.average((yy - mu) ** 2, weights=ww); mse = np.average((yy - pp) ** 2, weights=ww)
        nvm = np.average(nv, weights=ww)
        out.append((1 - mse / sst, 1 - max(mse - nvm, 0) / max(sst - nvm, 1e-12), spearmanr(yy, pp)[0], mse, nvm))
    return np.array(out).mean(0)


def main():
    codes, combos = classes()
    if not os.path.exists(W("X.npy")):
        from features import features
        Xf, names = features(codes); np.save(W("X.npy"), Xf); open(W("Xnames.txt"), "w").write("\n".join(names))
    X = np.load(W("X.npy"))
    w = combos.astype(np.float64)
    res = {}
    if os.path.exists(W("model_stats.json")):
        res = json.load(open(W("model_stats.json")))
    for b in range(NB):
        f = W(f"eqm_band{b:02d}.npz")
        if os.path.exists(f): continue
        t0 = time.time()
        d = np.load(W(f"sim_band{b:02d}.npz"))
        T = float(d["trials"]); y = d["eq"].astype(np.float64)
        nv = np.maximum(d["sq"] / T - y ** 2, 1e-4) / T
        pl = (d["ws"] - d["ls"]).astype(np.float64)
        nvp = np.maximum(d["ws"] + d["ls"] - pl ** 2, 1e-4) / T
        oof, fold = crossfit(X, y, w, b)
        st = stats(y, oof, nv, w, fold)
        tau2 = max(st[3] - st[4], 1e-7)                       # model error variance vs truth
        post = (y / nv + oof / tau2) / (1 / nv + 1 / tau2)
        pse = np.sqrt(1 / (1 / nv + 1 / tau2))
        oofp, foldp = crossfit(X, pl, w, 50 + b, rounds=200)
        stp = stats(pl, oofp, nvp, w, foldp)
        tau2p = max(stp[3] - stp[4], 1e-7)
        postp = (pl / nvp + oofp / tau2p) / (1 / nvp + 1 / tau2p)
        np.savez(f, eq=post.astype(np.float32), se=pse.astype(np.float32), pl=postp.astype(np.float32),
                 raw=y.astype(np.float32), model=oof.astype(np.float32))
        res[str(b)] = dict(r2_raw=st[0], r2_noise_corrected=st[1], spearman_raw=st[2],
                           rmse_model=float(np.sqrt(st[3])), sim_se=float(np.sqrt(st[4])),
                           model_err_vs_truth=float(np.sqrt(tau2)), post_se_mean=float(np.average(pse, weights=w)),
                           pl_r2_raw=stp[0], pl_r2_noise_corrected=stp[1], pl_spearman=stp[2])
        json.dump(res, open(W("model_stats.json"), "w"), indent=1)
        r = res[str(b)]
        progress(f"(3) band {b}: holdout R2 raw {r['r2_raw']:.4f} / noise-corrected {r['r2_noise_corrected']:.4f}, "
                 f"spearman raw {r['spearman_raw']:.4f}, sim SE {100*r['sim_se']:.2f} pts, model err {100*r['model_err_vs_truth']:.2f} pts, "
                 f"posterior SE {100*r['post_se_mean']:.2f} pts ({(time.time()-t0)/60:.1f} min)")
    precision_holdout(res)
    progress("(3) feature model + EB shrinkage done for all bands")


def precision_holdout(res, m=4000, trials=64000):
    """Re-simulate a random holdout of classes at high precision vs every band and score
    the out-of-fold model (and the EB posterior) against these near-exact targets."""
    import engine
    from stage2_sim import make_pool
    codes, combos = classes()
    band = np.load(W("bands.npz"))["band"]
    idx = np.random.default_rng(4242).choice(len(codes), m, replace=False)
    pools = []
    for b in range(NB):
        pool, _ = make_pool(np.where(band == b, combos, 0).astype(float), 100 + b)
        pools.append(pool)
    P = engine.Pools(pools)
    hero = np.tile(idx, NB); pid = np.repeat(np.arange(NB), m)
    s, sq, ws, ls, _ = engine.sim_tasks(codes[hero], pid, np.full(len(hero), trials), P, 31337)
    out = {}
    for b in range(NB):
        sel = pid == b
        truth = s[sel] / trials; tse = np.sqrt(np.maximum(sq[sel] / trials - truth ** 2, 1e-6) / trials)
        d = np.load(W(f"eqm_band{b:02d}.npz"))
        mod, post, raw = d["model"][idx], d["eq"][idx], d["raw"][idx]
        ww = combos[idx].astype(float)
        sst = np.average((truth - np.average(truth, weights=ww)) ** 2, weights=ww)
        r2 = lambda p: 1 - np.average((truth - p) ** 2, weights=ww) / sst
        rm = lambda p: float(np.sqrt(np.average((truth - p) ** 2, weights=ww)))
        out[str(b)] = dict(r2_model=float(r2(mod)), spearman_model=float(spearmanr(truth, mod)[0]),
                           r2_posterior=float(r2(post)), spearman_posterior=float(spearmanr(truth, post)[0]),
                           rmse_raw_sim=rm(raw), rmse_model=rm(mod), rmse_posterior=rm(post),
                           truth_se=float(tse.mean()))
        o = out[str(b)]
        progress(f"(3) precision holdout band {b} ({m} classes x {trials} boards, truth SE {100*o['truth_se']:.2f} pts): "
                 f"model R2 {o['r2_model']:.4f} spearman {o['spearman_model']:.4f}; posterior R2 {o['r2_posterior']:.4f} "
                 f"spearman {o['spearman_posterior']:.4f}; RMSE raw sim {100*o['rmse_raw_sim']:.2f} / model "
                 f"{100*o['rmse_model']:.2f} / posterior {100*o['rmse_posterior']:.2f} pts")
    res["precision_holdout"] = out
    json.dump(res, open(W("model_stats.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
