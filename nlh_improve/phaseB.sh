#!/bin/sh
# Phase B measurement gate: cost per postflop solve with 2 raises and with the river, per depth,
# and preflop seconds per iteration on the larger trees. Results in nlh_improve/work/phaseB.txt.
# One solve at a time on an otherwise idle machine (the real runs use 4 single-thread solves).
cd /home/user/claudecalc2/nlh_improve
until grep -q PHASE0_DONE phase0.log 2>/dev/null; do sleep 60; done
out=work/phaseB.txt
W=work/phaseB_$(date +%s); mkdir -p $W
echo "# Phase B measurements $(date -u)" >> $out
for d in 100:nlh9max 200:nlh9max_200bb 300:nlh9max_300bb; do
  stack=${d%%:*}; dir=../${d#*:}; R=$dir/work/final/ranges
  for spot in srp_BTN_vs_BB 3bp_BTN_vs_BB; do
    eval $(python3 -c "import json;s=json.load(open('$R/spots.json'))['$spot'];print(f\"POT={s['pot']} STK={s['stack']} DONK={s['donk']}\")")
    for flop in AcJc8d 9c7c6d; do
      for mode in "turn 1" "turn 2" "river 1" "river 2"; do
        set -- $mode
        te=1; [ $1 = river ] && te=0
        ae=$(python3 -c "import json;print(json.load(open('$dir/config.json')).get('postflop_allin_extra_frac',0))")
        tag=${stack}_${spot}_${flop}_$1_$2
        ./build/postflop $flop $POT $STK $R/${spot}_oop.f32 $R/${spot}_ip.f32 $W/$tag donk=$DONK threads=1 \
          maxit=40 target=0.0001 turnend=$te maxraises=$2 allin_extra=$ae mem_mb=12000 ckpt=0 > $W/$tag.out 2>&1
        echo "$tag $(tail -1 $W/$tag.out)" >> $out
      done
    done
  done
done
# preflop: seconds per iteration, 100 iterations each, on the three tree options
for t in 100_pruned 100_cold4 100_cold4+mw3bp 300_pruned 300_cold4 300_cold4+mw3bp; do
  D=$W/pf_$t; mkdir -p $D
  cp work/tree/${t}_tree.txt $D/tree.txt; cp work/tree/${t}_tree.json $D/tree.json; cp ../nlh9max/work/eq169.bin $D/
  NLH_CONFIG=work/tree/$t.json python3 src/rmodel.py fixed $D > /dev/null
  S=$(date +%s); ./build/preflop $D 100 $D/pre $D/rfile.bin 4 > /dev/null 2>&1
  echo "preflop $t: 100 iterations $(( $(date +%s) - S )) s" >> $out
done
echo PHASEB_DONE >> $out
