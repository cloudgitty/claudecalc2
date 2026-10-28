#!/usr/bin/env python3
"""Pilot report and projection (Part B2 task 8).

usage: python3 tools/pilot_report.py   (from the repo root)

Reads manifest/solves.tsv, manifest/pilot.txt and results/*.json.gz. For each tier:
median and slowest pilot solve time, peak memory, then the projected single-core
compute for the whole tier (per pot type: groups x (chip-EV time + 16 x ICM time),
using the tier's pilot solves of that pot type), what is left given the solves already
finished, and the number of 14-hour 4-core sessions (48 core-hours of solving each).
"""
import gzip
import json
import os
import statistics

CORE_HOURS_PER_SESSION = 48.0

solves = {}
with open("manifest/solves.tsv") as f:
    next(f)
    for line in f:
        t = line.rstrip("\n").split("\t")
        solves[t[0]] = {"tier": int(t[1][1:]), "pot": t[2], "spr": t[3], "mode": t[7]}

pilot = open("manifest/pilot.txt").read().strip().split(",")


def result(sid):
    p = f"results/{sid}.json.gz"
    if not os.path.exists(p):
        return None
    with gzip.open(p, "rt") as f:
        return json.load(f)


results = {sid: result(sid) for sid in solves}
done = {sid for sid, r in results.items() if r is not None}

print("Pilot solves")
print("| Solve | Tier | Mode | Iterations | Exploitability % (OOP / IP) | Seconds | Memory MB | Warm |")
print("| --- | --- | --- | --- | --- | --- | --- | --- |")
for sid in pilot:
    r = results.get(sid)
    if not r:
        print(f"| {sid} | T{solves[sid]['tier']} | {solves[sid]['mode']} | not solved yet | | | | |")
        continue
    e = r["exploitability_pct"]
    print(f"| {sid} | T{r['tier']} | {r['mode']} | {r['iterations']} | {e[0]:.3f} / {e[1]:.3f} | {r['seconds']:.0f} | "
          f"{r['peak_memory_mb']:.0f} | {'yes' if r.get('warm_start') else 'no'} |")

print()
print("| Tier | Pilot solves | Median s | Slowest s | Peak memory MB | Projected tier core-h | Solves done / total | Core-h left | Sessions left |")
print("| --- | --- | --- | --- | --- | --- | --- | --- | --- |")
total_left = 0.0
for tier in range(1, 6):
    ps = [results[s] for s in pilot if solves[s]["tier"] == tier and results.get(s)]
    secs = [r["seconds"] for r in ps]
    proj = 0.0
    ok = True
    for pot in ("srp", "3bp"):
        chip = [r["seconds"] for r in ps if r["pot_type"] == pot and r["mode"] == "chip"]
        icm = [r["seconds"] for r in ps if r["pot_type"] == pot and r["mode"] == "icm"]
        groups = sum(1 for s, v in solves.items() if v["tier"] == tier and v["pot"] == pot and v["mode"] == "chip")
        if not chip or not icm:
            ok = False
            continue
        proj += groups * (statistics.mean(chip) + 16 * statistics.mean(icm))
    tier_ids = [s for s, v in solves.items() if v["tier"] == tier]
    n_done = sum(1 for s in tier_ids if s in done)
    frac_left = 1 - n_done / len(tier_ids)
    left = proj * frac_left / 3600 if ok else float("nan")
    if ok:
        total_left += left
    peak = max([r["peak_memory_mb"] for r in ps], default=0)
    med = statistics.median(secs) if secs else float("nan")
    slow = max(secs) if secs else float("nan")
    print(f"| T{tier} | {len(ps)} | {med:.0f} | {slow:.0f} | {peak:.0f} | "
          f"{(proj / 3600 if ok else float('nan')):.1f} | {n_done} / {len(tier_ids)} | {left:.1f} | "
          f"{left / CORE_HOURS_PER_SESSION:.1f} |")
print()
spent = sum(r["seconds"] for r in results.values() if r) / 3600
print(f"Compute spent so far: {spent:.1f} core-hours over {len(done)} solves. "
      f"Projected core-hours left (T1-T5): {total_left:.0f} = {total_left / CORE_HOURS_PER_SESSION:.1f} sessions "
      f"of {CORE_HOURS_PER_SESSION:.0f} core-hours.")
