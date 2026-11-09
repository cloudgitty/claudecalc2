#!/bin/sh
# Phase D for one build (run from the build folder after src/pipeline2.py finished):
# a full 100-flop postflop pass on the delivered preflop (the last polish), packing, the section 5
# validation, the improvement-plan checks and the rake report. Resumable (finished solves are skipped).
set -e
E=$(cd "$(dirname "$0")/.." && pwd)
export NLH_CONFIG=work2/config2.json NLH_TREE=work2/tree.json
P=work2/polish2/pre; [ -f $P.strat ] || P=work2/polish/pre
D=work2/deliver; mkdir -p $D
[ -f $D/ranges/spots.json ] || python3 $E/src/spots.py $P $D/ranges > $D/spots.txt
python3 $E/src/run_postflop.py $D/ranges $D/solves all all 4 > $D/solves.log
python3 $E/src/pack.py preflop $P . > $D/pack.log
python3 $E/src/pack.py postflop $D/ranges $D/solves . >> $D/pack.log
R0=$(cat work2/rake0_last.txt); C3=$(cat work2/cap3_last.txt); P5=$(cat work2/pct5_last.txt)
python3 $E/src/validate.py . $P $D/ranges $D/solves $R0 > $D/validate.log 2>&1 || true
python3 $E/src/validate2.py validation.md > $D/validate2.log 2>&1 || true
python3 $E/src/rake_report.py rake_report.md $P $R0 "cap 3bb=$C3" "rake 5%=$P5" > $D/rake_report.log
echo PHASED_DONE >> $D/solves.log
