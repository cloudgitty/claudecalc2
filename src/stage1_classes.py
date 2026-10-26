"""Stage 1: class list, combos, evaluator self-test."""
import itertools, random, time, sys
import numpy as np
import engine
from common import W, progress

PERMS = list(itertools.permutations(range(4)))


def class_code_ref(cards):  # reference implementation from the spec
    best = None; ranks = None
    for p in PERMS:
        m = sorted(((r, p[s]) for r, s in cards), key=lambda x: (-x[0], x[1]))
        su = [x[1] for x in m]
        if best is None or su < best: best = su; ranks = [x[0] for x in m]
    v = 0
    for r in ranks: v = v * 13 + r
    for s in best: v = v * 4 + s
    return v


def stabilizer_combos(code):
    s = [(code >> (2 * (5 - i))) & 3 for i in range(6)]
    rv = code >> 12; r = []
    for _ in range(6): r.append(rv % 13); rv //= 13
    r = r[::-1]
    base = sorted(zip(r, s))
    fix = sum(1 for p in PERMS if sorted((rr, p[ss]) for rr, ss in zip(r, s)) == base)
    return 24 // fix


def eval5_ref(cards):  # independent slow evaluator for the self-test
    rk = sorted((c // 4 for c in cards), reverse=True)
    fl = len({c % 4 for c in cards}) == 1
    cnt = {r: rk.count(r) for r in set(rk)}
    groups = sorted(cnt.items(), key=lambda x: (-x[1], -x[0]))
    distinct = sorted(cnt, reverse=True)
    st = None
    if len(distinct) == 5:
        if distinct[0] - distinct[4] == 4: st = distinct[0]
        elif distinct == [12, 3, 2, 1, 0]: st = 3
    shape = [g[1] for g in groups]
    if st is not None and fl: cat = 8
    elif shape[0] == 4: cat = 7
    elif shape[:2] == [3, 2]: cat = 6
    elif fl: cat = 5
    elif st is not None: cat = 4
    elif shape[0] == 3: cat = 3
    elif shape[:2] == [2, 2]: cat = 2
    elif shape[0] == 2: cat = 1
    else: cat = 0
    tb = [st] if cat in (4, 8) else [g[0] for g in groups for _ in range(g[1])]
    return (cat, tb)


def omaha_ref(h, b):
    best = None
    for hp in itertools.combinations(h, 2):
        for bt in itertools.combinations(b, 3):
            v = eval5_ref(list(hp) + list(bt))
            if best is None or v > best: best = v
    return best


def main():
    t0 = time.time()
    rnd = random.Random(1)
    # evaluator self-test: 5-card ordering and Omaha best-hand ordering
    for _ in range(20000):
        a = rnd.sample(range(52), 5); b = rnd.sample(range(52), 5)
        ra, rb = eval5_ref(a), eval5_ref(b)
        ea, eb = engine.eval5(a), engine.eval5(b)
        assert (ra > rb) == (ea > eb) and (ra == rb) == (ea == eb), (a, b)
    for _ in range(3000):
        c = rnd.sample(range(52), 16)
        h, v, bd = c[:6], c[6:12], c[12:17] if len(c) > 16 else c[11:16]
        c = rnd.sample(range(52), 17); h, v, bd = c[:6], c[6:12], c[12:]
        rh, rv = omaha_ref(h, bd), omaha_ref(v, bd)
        eh, ev = engine.omaha_eval(h, bd), engine.omaha_eval(v, bd)
        assert (rh > rv) == (eh > ev) and (rh == rv) == (eh == ev)
    print("evaluator self-test ok", flush=True)

    codes_all = engine.enumerate_all()
    codes, combos = np.unique(codes_all, return_counts=True)
    print("classes", len(codes), "hands", combos.sum(), flush=True)
    assert len(codes) == 962988 and combos.sum() == 20358520
    assert codes.max() < 2 ** 35
    # C code vs reference implementation, and combos vs 24/|stabilizer|
    for _ in range(3000):
        c = rnd.sample(range(52), 6)
        ref = class_code_ref([(x // 4, x % 4) for x in c])
        assert ref == engine.class_codes(np.array(c))[0]
        i = np.searchsorted(codes, ref); assert codes[i] == ref
    idx = rnd.sample(range(len(codes)), 3000)
    for i in idx:
        assert stabilizer_combos(int(codes[i])) == combos[i]
    np.savez(W("classes.npz"), codes=codes, combos=combos.astype(np.int32))
    del codes_all
    progress(f"(1) class list: {len(codes)} classes, {combos.sum()} hands verified; evaluator self-test ok ({time.time()-t0:.0f}s)")


if __name__ == "__main__":
    main()
