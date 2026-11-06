#!/bin/sh
# One preflop <-> R round (section 2.5): ranges from <prev>, calibration solves, fit, preflop.
# usage: sh src/iterate.sh <prev_dir> <new_dir> [iterations=3000]
set -e
prev=$1; new=$2; it=${3:-3000}
mkdir -p $new
cp work/tree.json $new/
python3 src/spots.py $prev/pre $new/ranges
python3 src/run_postflop.py $new/ranges $new/solves calib all 4
cp $new/ranges/spots.json $new/spots_used.json
python3 src/fit_r.py $new/ranges $new/solves $new/fit.json
# damping: the new R is averaged with the previous round's (weight BLEND, default 0.5)
if [ -f $prev/rfile.bin ]; then
  R_BLEND=$prev/rfile.bin:${BLEND:-0.5} python3 src/rmodel.py fitted work $new/fit.json $prev/pre $new/rfile.bin
else
  python3 src/rmodel.py fitted work $new/fit.json $prev/pre $new/rfile.bin
fi
./build/preflop work $it $new/pre $new/rfile.bin 4
python3 src/preflop_report.py $new/pre --json $new/summary.json
