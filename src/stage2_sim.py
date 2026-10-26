"""Stage 2: strength ordering (pilot), reference bands, and equity simulation of
classes vs every reference band.  Resumable: each band is saved under work/."""
import os, sys, time
import numpy as np
import engine
from common import W, progress, classes

BAND_EDGES = [0, 1.5, 3, 5, 8, 12, 17, 23, 30, 40, 55, 75, 100]   # % of all hands, strength order
POOL = 400_000
BOARDS = int(os.environ.get("BOARDS", 4000))


def make_pool(weights, seed, n=POOL):
    """Concrete hands sampled by weight (class weight = freq * combos)."""
    codes, combos = classes()
    p = weights / weights.sum()
    rng = np.random.default_rng(seed)
    idx = rng.choice(len(codes), n, p=p)
    return engine.sample_reps(codes[idx], seed + 1), idx


def run_all(pool, trials, seed, label):
    codes, _ = classes()
    n = len(codes)
    P = engine.Pools([pool])
    t0 = time.time()
    s, sq, ws, ls = engine.sim_tasks(codes, np.zeros(n), np.full(n, trials), P, seed)
    dt = time.time() - t0
    print(f"{label}: {n*trials/dt/1e6:.1f}M trials/s, {n/dt:.0f} classes/s", flush=True)
    return s / trials, sq, ws / trials, ls / trials, dt


def main():
    codes, combos = classes()
    n = len(codes)
    t0 = time.time()
    if not os.path.exists(W("order.npz")):
        pool, _ = make_pool(combos.astype(float), 11)
        e0, *_ , dt = run_all(pool, 600, 12, "pilot vs random")
        progress(f"(2a) throughput {n*600/dt/1e6:.1f}M trials/s on {engine.threads()} threads "
                 f"=> one band at {BOARDS} boards for all {n} classes = {n*BOARDS/(n*600/dt)/60:.1f} min")
        o0 = np.argsort(-e0, kind="stable")
        cum = np.cumsum(combos[o0]) / combos.sum()
        w = np.zeros(n); w[o0[cum <= 0.25]] = combos[o0[cum <= 0.25]]
        pool, _ = make_pool(w, 13)
        e1, *_ = run_all(pool, 2000, 14, "pilot vs top25")
        np.savez(W("order.npz"), eq_random=e0.astype(np.float32), eq_top25=e1.astype(np.float32))
    d = np.load(W("order.npz"))
    strength = d["eq_top25"].astype(np.float64)
    order = np.argsort(-strength, kind="stable")
    cum = np.cumsum(combos[order]) / combos.sum() * 100
    band = np.empty(n, dtype=np.int8)
    for b in range(len(BAND_EDGES) - 1):
        sel = (cum > BAND_EDGES[b]) & (cum <= BAND_EDGES[b + 1] + 1e-9)
        band[order[sel]] = b
    band[order[0]] = 0
    pct = np.empty(n); pct[order] = cum                    # strength percentile (0 = best)
    np.savez(W("bands.npz"), band=band, pct=pct.astype(np.float32), edges=np.array(BAND_EDGES))
    nb = len(BAND_EDGES) - 1
    for b in range(nb):
        f = W(f"sim_band{b:02d}.npz")
        if os.path.exists(f): continue
        w = np.where(band == b, combos, 0).astype(float)
        pool, _ = make_pool(w, 100 + b)
        eq, sq, ws, ls, dt = run_all(pool, BOARDS, 200 + b, f"band {b}")
        np.savez(f, eq=eq.astype(np.float32), sq=sq.astype(np.float32), ws=ws.astype(np.float32),
                 ls=ls.astype(np.float32), trials=BOARDS)
        progress(f"(2b) band {b} [{BAND_EDGES[b]}-{BAND_EDGES[b+1]}%] simulated: {n} classes x {BOARDS} boards in {dt/60:.1f} min")
    progress(f"(2) simulation done: {n} classes x {nb} bands x {BOARDS} boards ({(time.time()-t0)/60:.0f} min)")


if __name__ == "__main__":
    main()
