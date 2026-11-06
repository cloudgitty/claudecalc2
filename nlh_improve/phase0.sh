#!/bin/sh
# Phase 0 (improvement plan item 1, interim): refit R on all 100 flops of each build's final
# postflop pass, blend 50/50 with the final R, re-solve preflop once. Resumable per depth.
for d in nlh9max nlh9max_200bb nlh9max_300bb; do
  cd /home/user/claudecalc2/$d || continue
  w=work/p0; mkdir -p $w; cp work/tree.json $w/
  if [ ! -f $w/summary.json ]; then
    cp work/final/ranges/spots.json $w/spots_used.json
    python3 src/fit_r.py work/final/ranges work/final/pre_solves $w/fit.json > $w/fit.txt 2>&1
    R_BLEND=work/final/rfile.bin:0.5 python3 src/rmodel.py fitted work $w/fit.json work/final/pre $w/rfile.bin > $w/rmodel.txt 2>&1
    ./build/preflop work 5000 $w/pre $w/rfile.bin 4 > /dev/null 2>&1
    python3 src/preflop_report.py $w/pre --json $w/summary.json > $w/report.txt 2>&1
  fi
  echo "$(date -u +%H:%M) $d phase 0: $(head -1 $w/report.txt) | before: $(head -1 work/final/report.txt 2>/dev/null)" >> /home/user/claudecalc2/nlh_improve/phase0.log
done
echo PHASE0_DONE >> /home/user/claudecalc2/nlh_improve/phase0.log
