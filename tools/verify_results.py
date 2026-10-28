#!/usr/bin/env python3
"""Checks results/ against the manifest (build-session task 1).

usage: python3 tools/verify_results.py   (from the repo root)

Every results/<id>.json.gz must be a manifest solve with matching tier, pot type, SPR,
flop, mode and seats; its players[] must hold both summaries; exploitability must be at
or under the 0.5% target. Also lists raw/ files without a result and leftover temp files.
"""
import gzip
import json
import os
import sys

solves = {}
with open("manifest/solves.tsv") as f:
    next(f)
    for line in f:
        t = line.rstrip("\n").split("\t")
        solves[t[0]] = {"tier": int(t[1][1:]), "pot": t[2], "spr": float(t[3]), "flop": t[4], "mode": t[7],
                        "oop": int(t[9]), "ip": int(t[10])}

problems = []
ok = 0
for name in sorted(os.listdir("results")):
    if not name.endswith(".json.gz"):
        problems.append(f"stray file results/{name}")
        continue
    sid = name[:-len(".json.gz")]
    m = solves.get(sid)
    if m is None:
        problems.append(f"{sid}: not in the manifest")
        continue
    try:
        with gzip.open(f"results/{name}", "rt") as f:
            r = json.load(f)
    except Exception as e:  # noqa: BLE001
        problems.append(f"{sid}: unreadable ({e})")
        continue
    checks = [r["id"] == sid, r["tier"] == m["tier"], r["pot_type"] == m["pot"], abs(r["spr"] - m["spr"]) < 1e-9,
              r["flop"] == m["flop"], r["mode"] == m["mode"], len(r.get("players", [])) == 2,
              max(r["exploitability_pct"]) <= 0.5 + 1e-9]
    if m["mode"] == "icm":
        checks += [r.get("oop_seat") == m["oop"], r.get("ip_seat") == m["ip"]]
    if all(checks):
        ok += 1
    else:
        problems.append(f"{sid}: fields do not match the manifest or target missed")
for name in sorted(os.listdir("raw")):
    sid = name[:-len(".bin")] if name.endswith(".bin") else None
    if sid is None or not os.path.exists(f"results/{sid}.json.gz"):
        problems.append(f"raw/{name}: no matching result")
print(f"{ok} results match the manifest ({len(solves)} solves in it); {len(problems)} problem(s)")
for p in problems:
    print("  " + p)
sys.exit(1 if problems else 0)
