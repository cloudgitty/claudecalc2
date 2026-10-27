"""Export the final 200bb ranges in the PokerTrainer app format:
app/plo6_ranges_packed_200bb.json, app/widths_200bb.json, app/test_vectors_200bb.json.
Source: work/final_bytes.npz (one byte per class per spot, written by stage7_pack.py)."""
import base64, json, os, random, zlib
import numpy as np
from common import W, R, classes
from solve import SPOT_ORDER
from stage1_classes import class_code_ref

STACK = 200
OUT = R("app")


def deflate(b):
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return c.compress(b) + c.flush()


def main():
    os.makedirs(OUT, exist_ok=True)
    codes, combos = classes()
    n = len(codes)
    assert n == 962988 and int(combos.sum()) == 20358520 and (np.diff(codes) > 0).all()
    B = np.load(W("final_bytes.npz"))
    prefix = f"plo6_6max_{STACK}bb/"
    planes = b"".join(((codes >> (8 * k)) & 0xFF).astype(np.uint8).tobytes() for k in range(4, -1, -1))
    js = {"n": n, "planes": 5, "classes": base64.b64encode(deflate(planes)).decode(),
          "spots": {prefix + k: base64.b64encode(deflate(B[k].astype(np.uint8).tobytes())).decode() for k in SPOT_ORDER}}
    with open(os.path.join(OUT, f"plo6_ranges_packed_{STACK}bb.json"), "w") as f:
        json.dump(js, f, separators=(",", ":"))
    # byte -> (call, raise) in percent
    tab = [(5 * c, 5 * r) for r in range(21) for c in range(21 - r)]
    cr = np.array(tab, dtype=np.float64)
    tot = combos.sum()
    widths = {}
    for k in SPOT_ORDER:
        c = cr[B[k], 0]; r = cr[B[k], 1]
        widths[k] = [round(float((combos * r).sum() / tot), 2), round(float((combos * (c + r)).sum() / tot), 2)]
    json.dump(widths, open(os.path.join(OUT, f"widths_{STACK}bb.json"), "w"), indent=1)
    rnd = random.Random(20261027)
    tv = []
    for _ in range(300):
        cards = sorted(rnd.sample(range(52), 6))
        code = class_code_ref([(x // 4, x % 4) for x in cards])
        cid = int(np.searchsorted(codes, code)); assert codes[cid] == code
        tv.append({"cards": cards, "code": int(code), "id": cid, "bytes": {k: int(B[k][cid]) for k in SPOT_ORDER}})
    json.dump(tv, open(os.path.join(OUT, f"test_vectors_{STACK}bb.json"), "w"), indent=1)
    print("exported", OUT)


if __name__ == "__main__":
    main()
