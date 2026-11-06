"""Runs postflop solves for a set of spots x flops, 4 at a time, resumable.

usage: python3 src/run_postflop.py <ranges_dir> <out_dir> <flops: all|calib|i,j,k> [spots: all|a,b] [jobs=4]

Ranges and pot / stack / donk flag come from <ranges_dir>/spots.json (src/spots.py).
Each solve writes <out_dir>/<spot>/<flop index>_<flop>.bin and .json (src/tools/postflop.c);
finished solves are skipped, so the command can be re-run after a restart.
Environment RAKE_PCT / RAKE_CAP override the rake (control and sensitivity runs); RIVER=1 solves (RIVER_REDUCED=<pot fraction>: one river bet size + all-in, no river raises)
flop to river instead of ending at the turn; CKPT_S sets the checkpoint interval (default 600 s);
MEM_MB sets the per-solve memory limit. Raises per street and the deep all-in rule come from the
config (postflop_max_raises, postflop_allin_extra_frac).
Calibration flops ("calib"): 12 of the 100, one from each of the largest texture groups
(listed in CALIB) - see REPORT.md.
"""
import json
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

CALIB = None  # filled from flops.json: every 8th flop by weight order, 12 flops (+ the top one)


def calib_indices(flops):
    idx = list(range(0, len(flops), 8))[:12]
    return idx


def main():
    rdir, out = sys.argv[1], sys.argv[2]
    which = sys.argv[3]
    spots_arg = sys.argv[4] if len(sys.argv) > 4 else "all"
    jobs = int(sys.argv[5]) if len(sys.argv) > 5 else 4
    flops = json.load(open("flops.json"))["flops"]
    cfg = json.load(open(os.environ.get("NLH_CONFIG", "config.json")))
    # solver options from the config (improvement plan items 7 and 8); RIVER=1 solves to the river
    extra = [f"maxraises={cfg.get('postflop_max_raises', 1)}",
             f"allin_extra={cfg.get('postflop_allin_extra_frac', 0.0)}",
             f"turnend={0 if os.environ.get('RIVER') == '1' else 1}",
             f"ckpt={os.environ.get('CKPT_S', '600')}"]
    if os.environ.get("RIVER") == "1" and os.environ.get("RIVER_REDUCED"):
        extra.append(f"river_reduced={os.environ['RIVER_REDUCED']}")
    if os.environ.get("MEM_MB"):
        extra.append(f"mem_mb={os.environ['MEM_MB']}")
    spots = json.load(open(os.path.join(rdir, "spots.json")))
    if which == "all":
        fidx = list(range(len(flops)))
    elif which == "calib":
        fidx = calib_indices(flops)
    else:
        fidx = [int(x) for x in which.split(",")]
    names = list(spots) if spots_arg == "all" else spots_arg.split(",")
    tasks = []
    for name in names:
        sp = spots[name]
        if min(sp.get("live", [1, 1])) == 0:
            print(f"{name}: skipped, a range is empty (not reached in the preflop equilibrium)", flush=True)
            continue
        os.makedirs(os.path.join(out, name), exist_ok=True)
        for i in fidx:
            f = flops[i]["flop"]
            prefix = os.path.join(out, name, f"{i:03d}_{f}")
            if os.path.exists(prefix + ".json"):
                continue
            cmd = ["./build/postflop", f, f"{sp['pot']:g}", f"{sp['stack']:g}",
                   os.path.join(rdir, f"{name}_oop.f32"), os.path.join(rdir, f"{name}_ip.f32"), prefix,
                   f"donk={sp['donk']}", "threads=1",
                   f"rake={os.environ.get('RAKE_PCT', '0.10')}", f"cap={os.environ.get('RAKE_CAP', '5')}"] + extra
            # largest pots first so the slow solves start early
            tasks.append((-sp["stack"] / sp["pot"], name, i, cmd))
    tasks.sort()
    print(f"{len(tasks)} solves to run, {jobs} at a time", flush=True)
    t0 = time.time()
    done = 0

    def run(t):
        r = subprocess.run(t[3], capture_output=True, text=True)
        return t, r

    with ThreadPoolExecutor(jobs) as ex:
        for t, r in ex.map(run, tasks):
            done += 1
            line = (r.stdout.strip() or r.stderr.strip()).splitlines()[-1] if (r.stdout or r.stderr) else ""
            print(f"[{done}/{len(tasks)} {time.time() - t0:7.0f}s] {t[1]} {line}", flush=True)


if __name__ == "__main__":
    main()
