"""Independent check of the app export, written from the format spec only
(no encoder code): inflate, rebuild codes from the 5 planes, decode bytes, and
compare 100,000 random (class, spot) pairs with the source work/final_bytes.npz.
Also replays the test vectors with the reference class-code algorithm."""
import base64, bisect, itertools, json, os, random, zlib
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, "app")
STACK = 200
SPOTS = ("LJ_RFI HJ_RFI CO_RFI BTN_RFI SB_RFI BB_vs_LJ BB_vs_HJ BB_vs_CO BB_vs_BTN BB_vs_SB SB_vs_CO "
         "SB_vs_BTN BTN_vs_CO LJ_vs_BB3bet HJ_vs_BB3bet CO_vs_BB3bet BTN_vs_BB3bet SB_vs_BB3bet "
         "CO_vs_BTN3bet BTN_vs_SB3bet BB_vs_BTN4bet BB_vs_SB4bet SB_vs_BTN4bet BTN_vs_CO4bet").split()


def inflate(s):
    return zlib.decompress(base64.b64decode(s), -15)


def main():
    path = os.path.join(APP, f"plo6_ranges_packed_{STACK}bb.json")
    js = json.load(open(path))
    n = js["n"]; assert n == 962988 and js["planes"] == 5
    raw = inflate(js["classes"]); assert len(raw) == 5 * n
    codes = [0] * n
    for k in range(5):
        plane = raw[k * n:(k + 1) * n]
        for i in range(n): codes[i] = (codes[i] << 8) | plane[i]
    assert all(codes[i] < codes[i + 1] for i in range(n - 1)), "codes not sorted/unique"
    # combos per class = number of the 24 suit relabellings giving distinct hands
    PERMS = list(itertools.permutations(range(4)))
    table = []
    for r in range(21):
        for c in range(21 - r): table.append((5 * c, 5 * r))
    keys = [f"plo6_6max_{STACK}bb/{s}" for s in SPOTS]
    assert sorted(js["spots"]) == sorted(keys), "spot keys differ from the spec"
    spots = {k: inflate(js["spots"][k]) for k in keys}
    for k, v in spots.items():
        assert len(v) == n
        assert max(v) <= 230 and all(sum(table[b]) <= 100 for b in set(v))
    src = np.load(os.path.join(ROOT, "work", "final_bytes.npz"))
    rnd = random.Random(424242); mism = 0
    for _ in range(100_000):
        cid = rnd.randrange(n); s = rnd.choice(SPOTS)
        if spots[f"plo6_6max_{STACK}bb/{s}"][cid] != int(src[s][cid]): mism += 1
    tv = json.load(open(os.path.join(APP, f"test_vectors_{STACK}bb.json"))); bad = 0
    for t in tv:
        cards = [(x // 4, x % 4) for x in t["cards"]]
        best = None
        for p in PERMS:
            m = sorted(((rk, p[su]) for rk, su in cards), key=lambda x: (-x[0], x[1]))
            su = [x[1] for x in m]
            if best is None or su < best: best = su; ranks = [x[0] for x in m]
        v = 0
        for rk in ranks: v = v * 13 + rk
        for su in best: v = v * 4 + su
        cid = bisect.bisect_left(codes, v)
        ok = codes[cid] == v == t["code"] and cid == t["id"]
        ok = ok and all(spots[f"plo6_6max_{STACK}bb/{k}"][cid] == b for k, b in t["bytes"].items())
        bad += not ok
    widths = json.load(open(os.path.join(APP, f"widths_{STACK}bb.json")))
    assert sorted(widths) == sorted(SPOTS) and all(len(v) == 2 and v[0] <= v[1] for v in widths.values())
    sizes = {f: os.path.getsize(os.path.join(APP, f)) for f in sorted(os.listdir(APP)) if f.endswith(".json")}
    print(f"n={n} codes sorted+unique; spots=24; max byte {max(max(v) for v in spots.values())}; "
          f"100,000 pairs vs source: {mism} mismatches; test vectors: {len(tv)} checked, {bad} failures")
    for f, b in sizes.items(): print(f"{f}: {b/1e6:.3f} MB")
    assert mism == 0 and bad == 0


if __name__ == "__main__":
    main()
