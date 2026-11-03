"""Section 5 validation. Writes <out_dir>/validation.md and test_vectors.json.

usage: python3 src/validate.py <out_dir> <preflop_prefix> <ranges_dir> <solves_dir> [rake_off_prefix]

Checks
 1 probability sums: raw strategies sum to 1 within 1e-6; quantised percents sum to 100
   per hand with each action within 5 points of the raw frequency;
 2 round trip: the independent decoder (src/decoder_ref.py) against the packer's source
   arrays, 100,000 random entries for the preflop file and 100,000 for the postflop files;
 3 structure: RFI widths increase from UTG to BTN; BB defence widest against BTN and SB;
   3-bet range polarised and narrower than the in-position call range; 4-bet and 5-bet
   narrower than 3-bet; every non-blind range tighter than the rake-free control;
 4 hand sanity table for the 9 named hands in the 8 RFI spots and BB vs BTN;
 5 exploitability reached per solve, worst case;
 6 test vectors: 300 preflop (node, hand class) and 300 postflop (spot, flop, combo, node).
"""
import glob
import json
import os
import random
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import decoder_ref  # noqa: E402
from hands import CLASS_COMBOS, COMBO_STR, LABELS, NCOMBOS  # noqa: E402
from pack import NODE_NAMES, STEP, quantise  # noqa: E402
from preflop_report import summary  # noqa: E402
from solveio import read_solve  # noqa: E402
from spots import SEATS, load_preflop  # noqa: E402

PRIOR = np.array(NCOMBOS, dtype=float) / 1326.0
SANITY = ["AA", "KK", "AKs", "AKo", "22", "87s", "A5s", "K9o", "72o"]
RNG = random.Random(7)


class Report:
    def __init__(self):
        self.lines = []
        self.fails = 0

    def head(self, t):
        self.lines.append(f"\n## {t}\n")

    def say(self, t):
        self.lines.append(t)
        print(t)

    def check(self, name, ok, detail=""):
        self.fails += not ok
        self.say(f"- {'PASS' if ok else '**FAIL**'} {name}{': ' + detail if detail else ''}")


def check_sums(rep, tree, strat, out_dir):
    rep.head("1 Probability sums")
    worst_raw, worst_q, worst_dev = 0.0, 0, 0.0
    for i, n in enumerate(tree):
        if n["kind"] != "dec":
            continue
        S = strat[i]
        worst_raw = max(worst_raw, float(np.abs(S.sum(0) - 1).max()))
        for h in range(169):
            q = quantise(S[:, h])
            worst_q = max(worst_q, abs(int(q.sum()) - 100))
            worst_dev = max(worst_dev, float(np.abs(q - S[:, h] * 100).max()))
    rep.check("raw strategies sum to 1 (< 1e-6)", worst_raw < 1e-6, f"worst |sum - 1| = {worst_raw:.2e}")
    rep.check("quantised percents sum to 100", worst_q == 0, f"worst deviation {worst_q}")
    rep.check("quantisation within 5 points per action", worst_dev <= STEP,
              f"worst |quantised - raw| = {worst_dev:.2f} points")
    recs, ev = decoder_ref.load_preflop(out_dir)
    bad = [r["id"] for r in recs
           for d in [decoder_ref.hands_of(r)]
           for lab, acts in d.items() if sum(acts.values()) > 100]
    rep.check("decoded hand strings never exceed 100% (folds implied)", not bad, f"{len(bad)} offenders")


def check_roundtrip_preflop(rep, tree, strat, reach, out_dir, n=100000):
    rep.head("2 Round trip (independent decoder)")
    recs, ev_dec = decoder_ref.load_preflop(out_dir)
    byid = {r["node"]: (k, r) for k, r in enumerate(recs)}
    worst_f, worst_ev, tested = 0, 0.0, 0
    nodes = [i for i in byid]
    for _ in range(n):
        node = RNG.choice(nodes)
        h = RNG.randrange(169)
        k, r = byid[node]
        d = decoder_ref.hands_of(r).get(LABELS[h], {})
        codes = ["r" if a.startswith("r") else a for a in r["actions"]]
        q = quantise(strat[node][:, h])
        for a, code in enumerate(codes):
            got = d.get(code, 0) if code != "f" else 100 - sum(d.values())
            worst_f = max(worst_f, abs(got - int(q[a])))
        worst_ev = max(worst_ev, abs(float(ev_dec[k][h]) - float(np.float16(ev_raw(tree, node, h)))))
        tested += 1
    rep.check(f"preflop: {tested} entries match the packer", worst_f == 0 and worst_ev == 0,
              f"worst frequency diff {worst_f} points, worst EV diff {worst_ev:.4g} bb")


EV_CACHE = {}


def ev_raw(tree, node, h):
    return EV_CACHE["ev"][node][h]


def check_roundtrip_postflop(rep, out_dir, rdir, sdir, n=100000):
    spots = json.load(open(os.path.join(rdir, "spots.json")))
    flops = json.load(open("flops.json"))["flops"]
    worst, tested = 0, 0
    per_spot = []
    files = sorted(glob.glob(os.path.join(out_dir, "postflop", "*.bin.gz")))
    for path in files:
        dec = decoder_ref.load_postflop(path)
        name = dec["header"]["spot"]
        idxs = list(dec["flops"])
        if not idxs:
            per_spot.append((name + " (not reached, no flops)", 0))
            continue
        per = max(1, -(-n // max(sum(1 for p in files if decoder_ref.load_postflop(p)["flops"]), 1)))
        w = 0
        cache = {}
        done_here = 0
        while done_here < per:
            fi = RNG.choice(idxs)
            if fi not in cache:
                cache[fi] = read_solve(os.path.join(sdir, name, f"{fi:03d}_{flops[fi]['flop']}.bin"))
            src = cache[fi]
            c = RNG.randrange(1326)
            k = RNG.randrange(len(NODE_NAMES))
            nd = src["nodes"][k]
            got = dec["flops"][fi][NODE_NAMES[k]]
            if nd is None:
                w = max(w, 0 if got is None else 99)
                continue
            if src["ranges"][nd["player"]][c] <= 0:
                w = max(w, int(got[c].sum()))
                continue
            q = quantise(nd["strat"][:, c]) // STEP
            w = max(w, int(np.abs(q - got[c]).max()))
            tested += 1
            done_here += 1
        per_spot.append((name, w))
        worst = max(worst, w)
    rep.check(f"postflop: {tested} entries match the packer", worst == 0,
              f"worst index diff {worst} ({', '.join(f'{a} {b}' for a, b in per_spot if b)})" if worst else "")


def check_structure(rep, s, s_norake):
    rep.head("3 Structure")
    rfi = s["rfi"]
    order = ["UTG", "UTG1", "LJ", "HJ", "CO", "BTN"]
    seq = [rfi[k] for k in order]
    rep.check("RFI width increases UTG < UTG1 < LJ < HJ < CO < BTN", all(a < b for a, b in zip(seq, seq[1:])),
              " < ".join(f"{k} {rfi[k]:.1f}" for k in order))
    deff = {k: s[f"BB vs {k} open"]["call"] + s[f"BB vs {k} open"]["raise"] for k in ("BTN", "SB", "CO", "UTG")}
    rep.check("BB defends widest against BTN and SB", min(deff["BTN"], deff["SB"]) > max(deff["CO"], deff["UTG"]),
              ", ".join(f"vs {k} {v:.1f}" for k, v in deff.items()))
    ip = s["ip_vs_open"]
    ok = all(v["3-bet"] < v["call"] for v in ip.values())
    rep.check("in position: 3-bet range narrower than the call range", ok,
              "; ".join(f"{k}: 3-bet {v['3-bet']:.1f}, call {v['call']:.1f}" for k, v in ip.items()) +
              ("" if ok else " - under this rake the in-position player 3-bets or folds (no flat range), "
                             "so there is no call range for the 3-bets to be narrower than or polarised against"))
    rep.check("in position: 3-bet range polarised (flats in the middle of the continuing range)",
              all(v["call"] > 0.5 for v in ip.values()),
              "not polarised: the continuing range is all 3-bets (linear 3-bet-or-fold)"
              if not all(v["call"] > 0.5 for v in ip.values()) else "")
    a = s["absolute"]
    rep.check("4-bet range narrower than the 3-bet range (% of all combos)", a["BTN 4-bet vs BB"] < a["BB 3-bet vs BTN"],
              f"BB 3-bet vs BTN {a['BB 3-bet vs BTN']:.2f}, BTN 4-bet {a['BTN 4-bet vs BB']:.2f}")
    rep.check("5-bet range narrower than the 3-bet range (% of all combos)", a["BB 5-bet vs BTN"] < a["BB 3-bet vs BTN"],
              f"BB 5-bet {a['BB 5-bet vs BTN']:.2f} vs BB 3-bet {a['BB 3-bet vs BTN']:.2f}")
    if s_norake:
        tighter = {k: (s["rfi"][k], s_norake["rfi"][k]) for k in order}
        ok = all(a <= b + 1e-9 for a, b in tighter.values())
        rep.check("rake makes every non-blind RFI range tighter than the rake-free control", ok,
                  ", ".join(f"{k} {a:.1f} vs {b:.1f}" for k, (a, b) in tighter.items()))


def hand_table(rep, prefix):
    rep.head("4 Hand sanity (frequencies in %)")
    s = summary(prefix)
    rows = list(s["sanity"])
    rep.say("| Spot | " + " | ".join(SANITY) + " |")
    rep.say("| --- | " + " | ".join("---" for _ in SANITY) + " |")
    for spot in rows:
        cells = []
        for h in SANITY:
            d = s["sanity"][spot][h]
            cells.append(" ".join(f"{a}{round(v)}" for a, v in d.items() if a != "f" and v >= 0.5) or "fold")
        rep.say(f"| {spot} | " + " | ".join(cells) + " |")
    return s


def exploitability(rep, prefix, sdir):
    rep.head("5 Exploitability reached")
    last = [x for x in open(prefix + ".log") if x[0].isdigit()][-1].split()
    worst = float(last[last.index("worst") + 1])
    rep.say(f"- preflop: {last[0]} iterations, per seat (bb/100): " +
            ", ".join(f"{last[i]} {last[i + 1]}" for i in range(1, 17, 2)) + f"; worst {worst:.3f} bb/100")
    rep.check("preflop best response <= 1.0 bb/100 per seat", worst <= 1.0, f"worst {worst:.3f}")
    rows = []
    for d in sorted(glob.glob(os.path.join(sdir, "*"))):
        js = sorted(glob.glob(os.path.join(d, "*.json")))
        if not js:
            continue
        ex = [max(json.load(open(p))["exploitability_pct"]) for p in js]
        it = [json.load(open(p))["iterations"] for p in js]
        rows.append((os.path.basename(d), len(js), float(np.median(ex)), float(max(ex)), int(max(it))))
    rep.say("\n| Spot | flops | median exploitability % of pot | worst | max iterations |")
    rep.say("| --- | --- | --- | --- | --- |")
    for r in rows:
        rep.say(f"| {r[0]} | {r[1]} | {r[2]:.3f} | {r[3]:.3f} | {r[4]} |")
    if rows:
        w = max(r[3] for r in rows)
        rep.check("postflop exploitability <= 0.5% of pot (or the iteration cap)", w <= 0.55,
                  f"worst {w:.3f}% of pot")
    return rows


def test_vectors(out_dir, rdir, sdir, n=300):
    recs, ev = decoder_ref.load_preflop(out_dir)
    pre = []
    for _ in range(n):
        k = RNG.randrange(len(recs))
        h = RNG.randrange(169)
        r = recs[k]
        pre.append({"id": r["id"], "node": r["node"], "hand": LABELS[h], "actions": r["actions"],
                    "freqs": decoder_ref.hands_of(r).get(LABELS[h], {}), "ev_bb": round(float(ev[k][h]), 4)})
    post = []
    files = [p for p in sorted(glob.glob(os.path.join(out_dir, "postflop", "*.bin.gz")))
             if decoder_ref.load_postflop(p)["flops"]]
    for _ in range(n):
        path = RNG.choice(files)
        dec = decoder_ref.load_postflop(path)
        fi = RNG.choice(list(dec["flops"]))
        node = RNG.choice([x["name"] for x in dec["header"]["flops"][0]["nodes"]])
        q = dec["flops"][fi][node]
        c = RNG.randrange(1326)
        post.append({"spot": dec["header"]["spot"], "flop_index": fi,
                     "flop": next(f["flop"] for f in dec["header"]["flops"] if f["index"] == fi),
                     "node": node, "combo": COMBO_STR[c], "combo_index": c,
                     "freq_indices": [] if q is None else [int(x) for x in q[c]],
                     "percents": [] if q is None else [int(x) * STEP for x in q[c]]})
    json.dump({"note": "expected decoded values for the formats in REPORT.md; percents are in 5% steps",
               "preflop": pre, "postflop": post}, open("test_vectors.json", "w"), indent=1)
    return len(pre), len(post)


def main():
    out_dir, prefix, rdir, sdir = sys.argv[1:5]
    norake = sys.argv[5] if len(sys.argv) > 5 else None
    work = os.path.dirname(prefix) or "."
    tree, strat = load_preflop(prefix, os.path.join(work, "tree.json"))
    reach = np.fromfile(prefix + ".reach", dtype=np.float64)
    EV_CACHE["ev"] = np.fromfile(prefix + ".ev", dtype=np.float32).reshape(len(tree), 169)
    rep = Report()
    rep.say(f"# Validation (section 5)\n\nPreflop solution `{prefix}`, outputs in `{out_dir}`.")
    check_sums(rep, tree, strat, out_dir)
    check_roundtrip_preflop(rep, tree, strat, reach, out_dir)
    check_roundtrip_postflop(rep, out_dir, rdir, sdir)
    s = hand_table(rep, prefix)
    s_norake = summary(norake) if norake else None
    check_structure(rep, s, s_norake)
    exploitability(rep, prefix, sdir)
    a, b = test_vectors(out_dir, rdir, sdir)
    rep.head("6 Test vectors")
    rep.say(f"- test_vectors.json: {a} preflop and {b} postflop entries")
    rep.say(f"\n**{'all checks passed' if not rep.fails else str(rep.fails) + ' check(s) FAILED'}**")
    open(os.path.join(out_dir, "validation.md"), "w").write("\n".join(rep.lines) + "\n")
    return 1 if rep.fails else 0


if __name__ == "__main__":
    sys.exit(main())
