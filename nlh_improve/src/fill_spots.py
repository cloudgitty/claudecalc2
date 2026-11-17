"""Fill postflop spots that the improved preflop never reaches, using the original build's ranges.

usage: python3 src/fill_spots.py <out_ranges_dir>      (run from the build folder, after Phase D)

The improved preflop is 3-bet-or-fold in several spots (e.g. BB vs a BTN open never flats), so
src/spots.py gives those spots an empty range and src/pack.py stores no flops for them. This copies
work2/deliver/ranges to <out_ranges_dir> and, for every spot with an empty range there whose range
in the original build (work/final/ranges) is not empty, puts in the original build's ranges and
marks the spot with a note. Pot, stack and donk rule are the same in both builds; the preflop node
id stays the improved tree's. Solve and pack afterwards with run_postflop.py / pack.py postflop on
<out_ranges_dir>. Prints the filled spot names, comma separated.
"""
import json
import os
import shutil
import sys

NOTE = ("not reached in the improved preflop (one player's range is empty there); solved on the "
        "original build's preflop ranges instead, with the improved postflop engine - the ranges do "
        "not match preflop.json (see REPORT.md section 0)")


def main():
    out = sys.argv[1]
    new_dir, old_dir = "work2/deliver/ranges", "work/final/ranges"
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(new_dir):
        shutil.copy(os.path.join(new_dir, f), os.path.join(out, f))
    new = json.load(open(os.path.join(new_dir, "spots.json")))
    old = json.load(open(os.path.join(old_dir, "spots.json")))
    filled = []
    for name, sp in new.items():
        if min(sp.get("live", [1, 1])) > 0:
            continue
        o = old.get(name)
        if o is None or min(o.get("live", [1, 1])) == 0:
            continue
        for k in ("pot", "stack", "donk"):
            assert abs(float(o[k]) - float(sp[k])) < 1e-6, (name, k, o[k], sp[k])
        for side in ("oop", "ip"):
            shutil.copy(os.path.join(old_dir, f"{name}_{side}.f32"), os.path.join(out, f"{name}_{side}.f32"))
        for k in ("oop_combos", "ip_combos", "trimmed_mass", "live"):
            sp[k] = o[k]
        sp["range_source"] = "original build"
        sp["note"] = NOTE
        filled.append(name)
    json.dump(new, open(os.path.join(out, "spots.json"), "w"), indent=1)
    print(",".join(filled))


if __name__ == "__main__":
    main()
