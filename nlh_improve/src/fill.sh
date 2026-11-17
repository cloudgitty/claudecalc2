#!/bin/sh
# Fill the spots the improved preflop never reaches (src/fill_spots.py), then re-pack and re-validate.
# Run from a build folder after Phase D: sh ../nlh_improve/src/fill.sh [jobs]. Resumable.
set -e
E=$(cd "$(dirname "$0")/.." && pwd)
J=${1:-2}
export NLH_CONFIG=work2/config2.json NLH_TREE=work2/tree.json
P=work2/polish2/pre; [ -f $P.strat ] || P=work2/polish/pre
D=work2/deliver; F=work2/fill
S=$(python3 $E/src/fill_spots.py $F/ranges)
echo "filling: $S"
[ -n "$S" ] && python3 $E/src/run_postflop.py $F/ranges $D/solves all "$S" $J > $F/solves.log
python3 $E/src/pack.py postflop $F/ranges $D/solves . > $F/pack.log
R0=$(cat work2/rake0_last.txt)
python3 $E/src/validate.py . $P $F/ranges $D/solves $R0 > $F/validate.log 2>&1 || true
python3 $E/src/validate2.py validation.md > $F/validate2.log 2>&1 || true
echo FILL_DONE
