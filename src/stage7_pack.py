"""Stage 7: quantise, pack (plo6_ranges_packed.json), widths.json, test_vectors.json,
and the section-8 structural validations (counts, monotonicity)."""
import base64, json, random, zlib, sys
import numpy as np
from common import W, R, progress, classes
from solve import Solver, SPOT_ORDER, OUTPUT
from stage5_iterate import load_r0
from stage1_classes import class_code_ref

PREFIX = "plo6_6max_200bb/"


def deflate(b):
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return c.compress(b) + c.flush()


def offset(r):
    return 21 * r - r * (r - 1) // 2


def to_bytes(c, r):
    ri = np.rint(r * 20).astype(np.int64); ci = np.rint(c * 20).astype(np.int64)
    ci = np.minimum(ci, 20 - ri)
    assert (ri >= 0).all() and (ci >= 0).all()
    return (21 * ri - ri * (ri - 1) // 2 + ci).astype(np.uint8)


def spot_bytes(state_path, R0=None, open_size=3.0):
    s = Solver(R0=R0 or load_r0(), open_size=open_size, log=lambda *a: None)
    s.load(state_path)
    out = {}
    for name in SPOT_ORDER:
        c, r = s.spot_freqs(name)
        out[name] = to_bytes(c, r)
    return s, out


def decode_byte_table():
    t = {}
    v = 0
    for r in range(21):
        for c in range(21 - r):
            t[v] = (5 * c, 5 * r); v += 1
    return t


def main(state=W("state_final.npz")):
    codes, combos = classes()
    n = len(codes)
    s, B = spot_bytes(state)
    # ---- packed json
    planes = b"".join(((codes >> (8 * k)) & 0xFF).astype(np.uint8).tobytes() for k in range(4, -1, -1))
    js = {"n": n, "planes": 5, "classes": base64.b64encode(deflate(planes)).decode(),
          "spots": {PREFIX + k: base64.b64encode(deflate(B[k].tobytes())).decode() for k in SPOT_ORDER}}
    with open(R("plo6_ranges_packed.json"), "w") as f: json.dump(js, f, separators=(",", ":"))
    # ---- widths
    tab = decode_byte_table()
    cr = np.array([tab[i] for i in range(231)])
    tot = combos.sum()
    widths = {}
    for k in SPOT_ORDER:
        c = cr[B[k], 0] / 100; r = cr[B[k], 1] / 100
        widths[PREFIX + k] = {"raise": round(100 * float((combos * r).sum() / tot), 2),
                              "continue": round(100 * float((combos * (c + r)).sum() / tot), 2)}
    json.dump(widths, open(R("widths.json"), "w"), indent=1)
    # ---- test vectors
    rnd = random.Random(20261026)
    tv = []
    for _ in range(300):
        cards = sorted(rnd.sample(range(52), 6))
        code = class_code_ref([(x // 4, x % 4) for x in cards])
        cid = int(np.searchsorted(codes, code)); assert codes[cid] == code
        tv.append({"cards": cards, "class_code": int(code), "class_id": cid,
                   "bytes": {PREFIX + k: int(B[k][cid]) for k in SPOT_ORDER}})
    json.dump(tv, open(R("test_vectors.json"), "w"), indent=1)
    np.savez_compressed(W("final_bytes.npz"), **B)
    # ---- structural validation
    val = {}
    val["n"] = n; val["combos_sum"] = int(combos.sum())
    val["max_byte"] = int(max(B[k].max() for k in SPOT_ORDER))
    val["all_bytes_valid"] = bool(val["max_byte"] <= 230)
    W_ = {k: widths[PREFIX + k] for k in SPOT_ORDER}
    rfi = [W_[f"{o}_RFI"]["raise"] for o in ["LJ", "HJ", "CO", "BTN"]]
    val["rfi_monotone LJ<HJ<CO<BTN"] = bool(all(a < b for a, b in zip(rfi, rfi[1:])))
    bb = [W_[f"BB_vs_{o}"]["continue"] for o in ["LJ", "HJ", "CO", "BTN"]]
    val["bb_defend_monotone LJ<HJ<CO<BTN"] = bool(all(a < b for a, b in zip(bb, bb[1:])))
    o3 = [W_[f"{o}_vs_BB3bet"]["continue"] for o in ["LJ", "HJ", "CO", "BTN"]]
    val["vs_BB3bet_continue_monotone"] = bool(all(a < b for a, b in zip(o3, o3[1:])))
    pairs4 = [("BB_vs_BTN4bet", "BTN_vs_BB3bet", "BB_vs_BTN"), ("BB_vs_SB4bet", "SB_vs_BB3bet", "BB_vs_SB"),
              ("SB_vs_BTN4bet", None, "SB_vs_BTN"), ("BTN_vs_CO4bet", "CO_vs_BTN3bet", "BTN_vs_CO")]
    chk = {}
    for s4, s3opp, s3 in pairs4:
        # the 4-bet range the 3-bettor faces is narrower than its own 3-bet range
        node, o, d = OUTPUT[s4]
        p = s.pairs[f"{o}-{d}"]
        w4 = (s.w * s.open[o] * s.S[p.key]["O2r"]).sum() / s.w.sum() * 100
        chk[s4] = (round(w4, 2), W_[s3]["raise"], bool(w4 < W_[s3]["raise"]))
    val["4bet_narrower_than_3bet (4bet width, 3bet width, ok)"] = chk
    bad = {}
    for k in ["LJ_vs_BB3bet", "HJ_vs_BB3bet", "CO_vs_BB3bet", "BTN_vs_BB3bet", "SB_vs_BB3bet", "CO_vs_BTN3bet", "BTN_vs_SB3bet"]:
        o = k.split("_")[0]
        raise_ = cr[B[k], 1] > 0; foldrfi = B[f"{o}_RFI"] == 0
        bad[k] = int((raise_ & foldrfi).sum())
    val["4bet raises that are pure RFI folds"] = bad
    json.dump(val, open(W("validation_struct.json"), "w"), indent=1)
    progress(f"(7) packed: {len(json.dumps(js))/1e6:.2f} MB; structural validation: {json.dumps(val)}")


if __name__ == "__main__":
    main(*(sys.argv[1:]))
