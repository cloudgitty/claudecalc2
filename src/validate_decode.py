"""Independent decoder written from the spec (does not import the encoder):
inflate, rebuild the class codes from the 5 byte planes, decode bytes to
(call %, raise %), and compare 100,000 random (class, spot) pairs with the
solver's final frequencies; also replays test_vectors.json."""
import base64, bisect, itertools, json, random, sys, zlib
import numpy as np
from common import W, R, progress


def inflate(b64):
    return zlib.decompress(base64.b64decode(b64), -15)


def main(state=W("state_final.npz")):
    js = json.load(open(R("plo6_ranges_packed.json")))
    n = js["n"]
    raw = inflate(js["classes"]); assert len(raw) == 5 * n
    codes = [0] * n
    for k in range(5):
        plane = raw[k * n:(k + 1) * n]
        for i in range(n): codes[i] = (codes[i] << 8) | plane[i]
    assert all(codes[i] < codes[i + 1] for i in range(n - 1)), "codes not strictly ascending"
    table = []
    for r in range(21):
        for c in range(21 - r): table.append((5 * c, 5 * r))
    spots = {k: inflate(v) for k, v in js["spots"].items()}
    assert all(len(v) == n for v in spots.values()) and len(spots) == 24
    for v in spots.values():
        assert max(v) < len(table) and all(table[b][0] + table[b][1] <= 100 for b in set(v))
    # reference frequencies straight from the solver state file
    st = np.load(state)
    from solve import OUTPUT
    def ref_freq(name, cid):
        node, o, d = OUTPUT[name]
        if node == "O1": return 0.0, float(st[f"open_{o}"][cid])
        key = f"{o}-{d}"
        if node == "D1": return float(st[f"{key}_D1c"][cid]), float(st[f"{key}_D1r"][cid])
        if node == "O2":
            if st[f"open_{o}"][cid] <= 0: return 0.0, 0.0
            return float(st[f"{key}_O2c"][cid]), float(st[f"{key}_O2r"][cid])
        if node == "D2":
            if st[f"{key}_D1r"][cid] <= 0: return 0.0, 0.0
            return float(st[f"{key}_D2c"][cid]), float(st[f"{key}_D2r"][cid])
    rnd = random.Random(99)
    names = list(spots)
    mism = 0
    for _ in range(100_000):
        cid = rnd.randrange(n); k = rnd.choice(names)
        c, r = table[spots[k][cid]]
        rc, rr = ref_freq(k.split("/")[1], cid)
        rr5 = round(rr * 20) * 5; rc5 = min(round(rc * 20) * 5, 100 - rr5)
        if (c, r) != (rc5, rr5): mism += 1
    # test vectors
    PERMS = list(itertools.permutations(range(4)))
    tv = json.load(open(R("test_vectors.json"))); tvbad = 0
    for t in tv:
        cards = [(x // 4, x % 4) for x in t["cards"]]
        best = None
        for p in PERMS:
            m = sorted(((rk, p[s]) for rk, s in cards), key=lambda x: (-x[0], x[1]))
            su = [x[1] for x in m]
            if best is None or su < best: best = su; ranks = [x[0] for x in m]
        v = 0
        for rk in ranks: v = v * 13 + rk
        for s in best: v = v * 4 + s
        cid = bisect.bisect_left(codes, v)
        ok = codes[cid] == v == t["class_code"] and cid == t["class_id"]
        ok &= all(spots[k][cid] == b for k, b in t["bytes"].items())
        tvbad += not ok
    res = dict(n=n, pairs_checked=100_000, mismatches=mism, test_vectors=len(tv), test_vector_failures=tvbad)
    json.dump(res, open(W("validation_decode.json"), "w"), indent=1)
    progress(f"(7) independent decoder: {res}")
    assert mism == 0 and tvbad == 0


if __name__ == "__main__":
    main(*(sys.argv[1:]))
