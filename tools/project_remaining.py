#!/usr/bin/env python3
"""Remaining library compute, projected from every finished solve (Part B2 wrap-up).

usage: python3 tools/project_remaining.py   (from the repo root)

Model: solve seconds scale with solver memory (tree size x combos), so for each
(pot type, SPR, mode) the build's measured seconds per MB of solver memory is applied to
the expected memory of every unsolved solve. Mode is chip EV, cold ICM or warm ICM;
the plan warm-starts ICM solves at SPR 3.5 and 6, so those use the warm rate (cold if no
warm solve has been measured). Expected memory = B1's Ks7d2c memory for the pot type and
SPR x the flop's tree-size factor (B1, measured on all 18 representative flops).
Rates come from solves run 4 at a time, i.e. real build conditions. Sessions assume 48
core-hours of solving per 14-hour session (12 hours x 4 cores).
"""
import gzip
import json
import os
from collections import defaultdict

CORE_HOURS_PER_SESSION = 48.0

# B1: solver memory (MB) on Ks7d2c with the placeholder ranges (SRP SPR 6 in 16-bit storage)
KS7D2C_MB = {("srp", "1.5"): 490, ("srp", "3.5"): 2184, ("srp", "6"): 3568,
             ("3bp", "1.5"): 170, ("3bp", "3.5"): 628, ("3bp", "6"): 1879}
# B1: tree memory of each representative flop relative to Ks7d2c (SRP SPR 3.5)
FLOP_FACTOR = {
    "Ks7d2c": 1.00, "Ah8d3c": 0.97, "Qs6d2h": 1.02, "9s5d2c": 1.02, "8h4d3c": 1.02, "7s3d2h": 1.04,
    "KsKd4c": 0.61, "8s8d3h": 0.63, "5s5dAc": 0.62, "AsJs5d": 0.58, "Qh9h4c": 0.60, "Td6d2s": 0.62,
    "Ks9s4s": 0.28, "Jh7h3h": 0.29, "AdTd5d": 0.28, "9s8d7c": 0.98, "JsTd8h": 0.97, "6s5d4h": 1.03,
}

solves = []
with open("manifest/solves.tsv") as f:
    next(f)
    for line in f:
        t = line.rstrip("\n").split("\t")
        solves.append({"id": t[0], "tier": int(t[1][1:]), "pot": t[2], "spr": t[3], "flop": t[4], "mode": t[7]})


def load(sid):
    p = f"results/{sid}.json.gz"
    if not os.path.exists(p):
        return None
    with gzip.open(p, "rt") as f:
        return json.load(f)


sec_sum = defaultdict(float)
mb_sum = defaultdict(float)
done = {}
for s in solves:
    r = load(s["id"])
    if r is None:
        continue
    done[s["id"]] = r
    mode = s["mode"] if s["mode"] == "chip" else ("warm" if r.get("warm_start") else "cold")
    key = (s["pot"], s["spr"], mode)
    sec_sum[key] += r["seconds"]
    mb_sum[key] += r["peak_memory_mb"]


def rate(pot, spr, mode):
    for m in ([mode] + (["cold"] if mode == "warm" else ["warm"] if mode == "cold" else [])):
        if mb_sum.get((pot, spr, m)):
            return sec_sum[(pot, spr, m)] / mb_sum[(pot, spr, m)], m
    return None, None


print("Measured rates (seconds per MB of solver memory, from finished solves):")
for key in sorted(sec_sum):
    print(f"  {key[0]} SPR {key[1]:>3} {key[2]:<5}: {sec_sum[key] / mb_sum[key]:.3f} s/MB "
          f"over {mb_sum[key]:.0f} MB of solves")

print()
print("| Tier | Solves done / total | Core-h spent | Core-h left (projected) | Sessions left |")
print("| --- | --- | --- | --- | --- |")
tot_left = tot_spent = 0.0
missing = set()
for tier in range(1, 6):
    ids = [s for s in solves if s["tier"] == tier]
    spent = sum(done[s["id"]]["seconds"] for s in ids if s["id"] in done) / 3600
    left = 0.0
    for s in ids:
        if s["id"] in done:
            continue
        mode = "chip" if s["mode"] == "chip" else ("cold" if s["spr"] == "1.5" else "warm")
        rt, used = rate(s["pot"], s["spr"], mode)
        if rt is None:
            missing.add((s["pot"], s["spr"], mode))
            continue
        left += rt * KS7D2C_MB[(s["pot"], s["spr"])] * FLOP_FACTOR[s["flop"]] / 3600
    tot_left += left
    tot_spent += spent
    n_done = sum(1 for s in ids if s["id"] in done)
    print(f"| T{tier} | {n_done} / {len(ids)} | {spent:.1f} | {left:.1f} | {left / CORE_HOURS_PER_SESSION:.1f} |")
print(f"| **Total** | {len(done)} / {len(solves)} | {tot_spent:.1f} | {tot_left:.0f} | "
      f"{tot_left / CORE_HOURS_PER_SESSION:.1f} |")
if missing:
    print("No measured rate for:", sorted(missing))
