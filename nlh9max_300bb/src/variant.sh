#!/bin/sh
# Rake variant (control / sensitivity, section 5 and output C): one R round under another rake.
# usage: sh src/variant.sh <name> <rake_pct> <rake_cap> [base=work/final]
# Postflop calibration solves (14 spots x 12 flops) with the variant's rake on the base ranges,
# R and expected rake fitted from them, then the preflop solve with the variant's rake.
set -e
name=$1; pct=$2; cap=$3; base=${4:-work/final}
d=work/var_$name
mkdir -p $d
cp work/tree.json $d/
python3 src/spots.py $base/pre $d/ranges
RAKE_PCT=$pct RAKE_CAP=$cap python3 src/run_postflop.py $d/ranges ${d}/pre_solves calib all 4
cp $d/ranges/spots.json $d/spots_used.json
python3 src/fit_r.py $d/ranges ${d}/pre_solves $d/fit.json
python3 - "$d" "$pct" "$cap" <<'PY'
import json, sys
d, pct, cap = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
c = json.load(open("config.json"))
c["rake_pct"], c["rake_cap_bb"] = pct, cap
json.dump(c, open(f"{d}/config.json", "w"), indent=1)
PY
NLH_CONFIG=$d/config.json python3 src/rmodel.py fitted work $d/fit.json $base/pre $d/rfile.bin
./build/preflop work 5000 $d/pre $d/rfile.bin 4 $pct $cap
python3 src/preflop_report.py $d/pre --json $d/summary.json
