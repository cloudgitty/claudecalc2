#!/bin/sh
# Phase B part 2: reduced river tree cost (one river size 66% + all-in, no river raises), then preflop timing
cd /home/user/claudecalc2/nlh_improve
out=work/phaseB.txt; W=work/phaseB2_$(date +%s); mkdir -p $W
echo "# Phase B part 2 (reduced river) $(date -u)" >> $out
for d in 100:nlh9max 200:nlh9max_200bb 300:nlh9max_300bb; do
  stack=${d%%:*}; dir=../${d#*:}; R=$dir/work/final/ranges
  ae=$(python3 -c "import json;print(json.load(open('$dir/config.json')).get('postflop_allin_extra_frac',0))")
  for spot in srp_BTN_vs_BB 3bp_BTN_vs_BB; do
    eval $(python3 -c "import json;s=json.load(open('$R/spots.json'))['$spot'];print(f\"POT={s['pot']} STK={s['stack']} DONK={s['donk']}\")")
    for mr in 1 2; do
      tag=${stack}_${spot}_AcJc8d_rred_$mr
      ./build/postflop AcJc8d $POT $STK $R/${spot}_oop.f32 $R/${spot}_ip.f32 $W/$tag donk=$DONK threads=1 \
        maxit=40 target=0.0001 turnend=0 maxraises=$mr allin_extra=$ae river_reduced=0.66 mem_mb=14000 ckpt=0 > $W/$tag.out 2>&1
      echo "$tag $(tail -1 $W/$tag.out)" >> $out
    done
  done
done
for t in 100_pruned 100_cold4 100_cold4+mw3bp 300_pruned 300_cold4 300_cold4+mw3bp; do
  D=$W/pf_$t; mkdir -p $D
  cp work/tree/${t}_tree.txt $D/tree.txt; cp work/tree/${t}_tree.json $D/tree.json; cp ../nlh9max/work/eq169.bin $D/
  NLH_CONFIG=work/tree/$t.json python3 src/rmodel.py fixed $D > /dev/null
  S=$(date +%s); ./build/preflop $D 100 $D/pre $D/rfile.bin 4 > /dev/null 2>&1
  echo "preflop $t: 100 iterations $(( $(date +%s) - S )) s" >> $out
done
echo PHASEB2_DONE >> $out
