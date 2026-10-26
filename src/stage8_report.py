"""Stage 8b: assemble REPORT.md from the stage outputs (numbers are read, not typed)."""
import json, os, re, time
import numpy as np
from common import W, R, classes
from solve import SPOT_ORDER, OUTPUT, Solver
from stage5_iterate import load_r0

NOTES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "report_notes.md")


def fmt_hist(h, tag):
    rows = ["| round | max width change (pts) | mean byte-freq change (pts) | worst spot (pts) | LJ / HJ / CO / BTN / SB open % |",
            "|---|---|---|---|---|"]
    for x in h:
        w = x["widths"]
        rows.append(f"| {tag} {x['round']} | {x['dw']:.2f} | {x['mean_df']:.2f} | {x['max_df']:.2f} | " +
                    " / ".join(f"{w[o + '_RFI'][0]:.1f}" for o in ["LJ", "HJ", "CO", "BTN", "SB"]) + " |")
    return "\n".join(rows)


def main():
    codes, combos = classes()
    w = combos.astype(float); tot = w.sum()
    B = np.load(W("final_bytes.npz"))
    tab = []
    for r in range(21):
        for c in range(21 - r): tab.append((c / 20, r / 20))
    tab = np.array(tab)
    s = Solver(R0=load_r0(), log=lambda *a: None); s.load(W("state_final.npz"))
    widths = json.load(open(R("widths.json")))
    ms = json.load(open(W("model_stats.json")))
    r0 = json.load(open(W("r0.json")))
    rs = json.load(open(W("resim_stats.json")))
    vs = json.load(open(W("validation_struct.json")))
    vd = json.load(open(W("validation_decode.json")))
    ck = json.load(open(W("checks.json")))
    hm = json.load(open(W("hist_main.json"))); hr = json.load(open(W("hist_refine.json")))
    edges = list(np.load(W("bands.npz"))["edges"])
    # ---- spot table
    srows = ["| spot | classes played | raise % of all hands | continue % of all hands | reach % of all hands | raise % of reach | continue % of reach |",
             "|---|---|---|---|---|---|---|"]
    for k in SPOT_ORDER:
        b = B[k]; c = tab[b, 0]; r = tab[b, 1]
        node, o, d = OUTPUT[k]
        if node in ("O1", "D1"): reach = w
        elif node == "O2": reach = w * s.open[o]
        else: reach = w * s.S[f"{o}-{d}"]["D1r"]
        rt = reach.sum()
        srows.append(f"| {k} | {int((b > 0).sum()):,} | {100*(w*r).sum()/tot:.2f} | {100*(w*(c+r)).sum()/tot:.2f} | "
                     f"{100*rt/tot:.2f} | {100*(w*r).sum()/rt:.1f} | {100*(w*(c+r)).sum()/rt:.1f} |")
    # ---- model table
    mrows = ["| band | range (% of hands, strength order) | holdout R2 raw (4k-board targets) | noise-corrected R2 | Spearman raw | precision-holdout R2 model | precision-holdout Spearman model | RMSE raw sim / model / posterior (pts) |",
             "|---|---|---|---|---|---|---|---|"]
    for b in range(12):
        x = ms[str(b)]; p = ms["precision_holdout"][str(b)]
        mrows.append(f"| {b} | {edges[b]:g}-{edges[b+1]:g} | {x['r2_raw']:.4f} | {x['r2_noise_corrected']:.4f} | {x['spearman_raw']:.4f} | "
                     f"{p['r2_model']:.4f} | {p['spearman_model']:.4f} | {100*p['rmse_raw_sim']:.2f} / {100*p['rmse_model']:.2f} / {100*p['rmse_posterior']:.2f} |")
    # ---- hands
    hrows = []
    for h in ck["hands"]:
        nz = [f"{k} {c}/{r}" for k, (c, r) in h["freqs"].items() if c or r]
        hrows.append(f"- **{h['label']}** ({h['cards']}, class {h['class_id']}): " + ("; ".join(nz) if nz else "fold everywhere"))
    # ---- sensitivity
    sens = ck.get("sensitivity", {})
    sh = "| spot | " + " | ".join(sens) + " |"
    srows2 = [sh, "|---|" + "---|" * len(sens)]
    for k in SPOT_ORDER:
        srows2.append(f"| {k} | " + " | ".join(f"{sens[l]['delta'][k][0]:+.2f} / {sens[l]['delta'][k][1]:+.2f}" for l in sens) + " |")
    conv_line = "; ".join(f"{l}: converged={sens[l]['converged']} in {sens[l]['rounds']} rounds" for l in sens)
    fp = ck["first_principles"]
    ex = "\n".join(f"| {e['hero']} | {e['villain']} | {100*e['exact']:.3f} | {100*e['mc_400k']:.3f} | {e['z']:+.2f} |" for e in fp["exact_vs_mc"])
    files = []
    for f in ["plo6_ranges_packed.json", "widths.json", "test_vectors.json"]:
        files.append(f"| {f} | {os.path.getsize(R(f))/1e6:.3f} MB |")
    js = json.load(open(R("plo6_ranges_packed.json")))
    cl = len(js["classes"]) / 1e6
    sp = sum(len(v) for v in js["spots"].values()) / 1e6
    notes = open(NOTES).read() if os.path.exists(NOTES) else ""
    rep = f"""# PLO6 6-max 200bb preflop ranges: report

{notes.split('<!--AUTO-->')[0] if notes else ''}
## Summary per spot

Widths are combo-weighted shares of all 20,358,520 hands. "Reach" is the share of all
hands that get to the decision: all hands for RFI and defend spots, the opening range
for opener vs 3-bet, and the 3-bet range for 3-bettor vs 4-bet.

{chr(10).join(srows)}

## Convergence log

Main iteration (damping 0.5, tolerance: width change <= 0.3 pts and mean byte-frequency
change <= 2 pts, two rounds in a row; the frequency change is the combo-weighted mean of
|d call| + |d raise| over classes in the spot's range, averaged over the 24 spots):

{fmt_hist(hm, 'main')}

Refinement rounds after the borderline re-simulation (exact equities in place):

{fmt_hist(hr, 'refine')}

## Validation

**Counts and encoding.** n = {vs['n']:,} classes, combos sum = {vs['combos_sum']:,},
largest byte = {vs['max_byte']} (every byte decodes to call + raise <= 100: {vs['all_bytes_valid']}).

**Round trip.** An independent decoder (`src/validate_decode.py`, written from the spec, with no
encoder code) inflates the planes, checks the 962,988 codes are strictly ascending, and compares
{vd['pairs_checked']:,} random (class, spot) pairs with the solver state: {vd['mismatches']} mismatches.
All {vd['test_vectors']} test vectors replay (class code by the reference algorithm, class id by
binary search, all 24 bytes): {vd['test_vector_failures']} failures.

**Monotonic sanity.** {json.dumps({k: v for k, v in vs.items() if k not in ('n', 'combos_sum', 'max_byte', 'all_bytes_valid')})}

**Feature model** (LightGBM on 65 hand-structure features, 5-fold cross-fit so that every
class's model value comes from a model that did not see it; each fold is a 20% holdout).
The raw holdout targets are 4,000-board simulations whose own noise (about 0.75 pts SE) caps
the R2 any model can reach against them, so the table also scores the out-of-fold model on a
precision holdout: 4,000 random classes re-simulated at 64,000 boards per band (target SE about 0.19 pts).

{chr(10).join(mrows)}

Playability model (nut-minus-reverse-nut potential), noise-corrected holdout R2 per band:
{", ".join(f"{ms[str(b)]['pl_r2_noise_corrected']:.3f}" for b in range(12))}.

**Borderline re-simulation.** Band = classes within +-{100*rs['half']:.2f}% of reaching combos of a
cutoff (RFI +-{100*rs['half_rfi']:.2f}%), in every output spot. Classes per spot: {json.dumps(rs['bands'])}.
{rs['tasks']:,} (class, actual range) pairs, {rs['trials']:.3e} trials; max SE {100*rs['max_se']:.3f} pts
(mean {100*rs['mean_se']:.3f}). Band-mixture approximation vs the exact equity against the actual range:
bias {100*rs['mixture_bias']:+.2f} pts, RMSE {100*rs['mixture_rmse']:.2f} pts, 95th percentile |error|
{100*rs['mixture_p95_abs']:.2f} pts.

**First-principles checks** (no published numbers are quoted):
- Mean equity of a random hand vs a random hand must be exactly 50%: raw simulations
  {100*fp['mean_eq_random_vs_random_raw']:.3f}%, shrunk table {100*fp['mean_eq_random_vs_random_posterior']:.3f}%.
- Band a vs band b plus band b vs band a must be about 1: max deviation {fp['max_|eq(a,b)+eq(b,a)-1|']:.4f}.
- Exact enumeration of all C(40,5) = 658,008 boards vs the Monte Carlo sampler:

| hero | villain | exact eq % | MC eq % (400k) | z |
|---|---|---|---|---|
{ex}

## Hand-level sanity (call % / raise %, spots where the hand plays)

{chr(10).join(hrows)}

## Sensitivity (change in raise / continue width, pts of all hands)

{conv_line}. R0 +-0.05 shifts every R0 value; open +-0.5bb changes the open size (SB to 4 +- 0.5)
with 3-bet sizes and the pot-sized 4-bet unchanged in formula. Each rerun warm-starts from the final
state with the same equity corrections.

{chr(10).join(srows2)}

{notes.split('<!--AUTO-->')[1] if '<!--AUTO-->' in notes else ''}

## Files

| file | size |
|---|---|
{chr(10).join(files)}

Packed JSON: classes field {cl:.2f} MB (base64), 24 spot fields {sp:.2f} MB in total.
"""
    open(R("REPORT.md"), "w").write(rep)
    print("REPORT.md written")


if __name__ == "__main__":
    main()
