#!/bin/sh
# Further damped preflop <-> R round for a rake variant (same as src/iterate.sh, with the variant's
# rake in the postflop solves, the R fit and the preflop solve).
# usage: sh src/variant_iter.sh <prev_dir> <new_dir> <rake_pct> <rake_cap> [iterations=5000]
set -e
prev=$1; new=$2; pct=$3; cap=$4; it=${5:-5000}
mkdir -p $new
cp work/tree.json $new/
cp $prev/config.json $new/config.json
python3 src/spots.py $prev/pre $new/ranges
RAKE_PCT=$pct RAKE_CAP=$cap python3 src/run_postflop.py $new/ranges $new/pre_solves calib all 4
cp $new/ranges/spots.json $new/spots_used.json
python3 src/fit_r.py $new/ranges $new/pre_solves $new/fit.json
R_BLEND=$prev/rfile.bin:${BLEND:-0.5} NLH_CONFIG=$new/config.json python3 src/rmodel.py fitted work $new/fit.json $prev/pre $new/rfile.bin
./build/preflop work $it $new/pre $new/rfile.bin 4 $pct $cap
python3 src/preflop_report.py $new/pre --json $new/summary.json
