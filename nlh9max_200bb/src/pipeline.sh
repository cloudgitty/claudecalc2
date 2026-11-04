#!/bin/sh
# Full 200bb build after the equity table (stages 3-8). Resumable: each step is skipped when its
# output exists. usage: sh src/pipeline.sh  (from nlh9max_200bb/)
set -e
log() { echo "$(date -u +%H:%M) $*" >> work/pipeline.log; }
w=work

# stage 3: fixed-R preflop solve
if [ ! -f $w/s3/pre.strat ]; then
  mkdir -p $w/s3; cp $w/tree.json $w/s3/
  python3 src/rmodel.py fixed $w
  ./build/preflop $w 3000 $w/s3/pre $w/rfile.bin 4
  python3 src/preflop_report.py $w/s3/pre --json $w/s3/summary.json > $w/s3/report.txt
  log "stage 3 done: $(head -1 $w/s3/report.txt)"
fi

# stage 4 + 5: calibration solves and R fit on the stage-3 ranges
if [ ! -f $w/s4/rfile.bin ]; then
  mkdir -p $w/s4
  python3 src/spots.py $w/s3/pre $w/s3/ranges > $w/s4/spots.txt
  python3 src/run_postflop.py $w/s3/ranges $w/s4/solves calib all 4 > $w/s4/run.log 2>&1
  cp $w/s3/ranges/spots.json $w/s4/spots_used.json
  python3 src/fit_r.py $w/s3/ranges $w/s4/solves $w/s4/fit.json > $w/s4/fit.txt
  python3 src/rmodel.py fitted $w $w/s4/fit.json $w/s3/pre $w/s4/rfile.bin > $w/s4/rmodel.txt
  log "stage 4-5 done: $(grep -c '^\[' $w/s4/run.log) calibration solves"
fi

# preflop <-> R rounds: n1 uses the stage-4 R, n2..n5 refit with damping
if [ ! -f $w/n1/pre.strat ]; then
  mkdir -p $w/n1; cp $w/tree.json $w/n1/; cp $w/s4/rfile.bin $w/n1/rfile.bin
  ./build/preflop $w 3000 $w/n1/pre $w/n1/rfile.bin 4
  python3 src/preflop_report.py $w/n1/pre --json $w/n1/summary.json > $w/n1.log
  log "round n1: $(head -1 $w/n1.log)"
fi
prev=n1
for r in n2 n3 n4 n5; do
  if [ ! -f $w/$r/summary.json ]; then
    sh src/iterate.sh $w/$prev $w/$r > $w/$r.log 2>&1
    log "round $r: $(grep RFI $w/$r.log)"
  fi
  prev=$r
done

# stage 6: final preflop solve with the mean R of rounds n3-n5
if [ ! -f $w/final/pre.strat ]; then
  mkdir -p $w/final; cp $w/tree.json $w/final/
  python3 - <<'PY'
import sys, numpy as np
sys.path.insert(0, 'src')
from rmodel import read_rfile_full, write_rfile
runs = [read_rfile_full(f'work/{d}/rfile.bin') for d in ('n3', 'n4', 'n5')]
out = []
for es in zip(*runs):
    out.append((es[0][0], float(np.mean([e[1] for e in es])),
                [np.mean([e[2][k] for e in es], axis=0) for k in range(len(es[0][2]))]))
write_rfile('work/final/rfile.bin', out)
PY
  ./build/preflop $w 5000 $w/final/pre $w/final/rfile.bin 4
  python3 src/preflop_report.py $w/final/pre --json $w/final/summary.json > $w/final/report.txt
  log "stage 6 done: $(head -1 $w/final/report.txt)"
fi

# stage 7: full postflop pass
python3 src/spots.py $w/final/pre $w/final/ranges > $w/final/spots.txt
python3 src/run_postflop.py $w/final/ranges $w/final/pre_solves all all 4 > $w/stage7.log 2>&1
log "stage 7 done: $(grep -c '^\[' $w/stage7.log) solves"

# rake variants (control and sensitivity)
for v in "rake0 0 0" "cap3 0.10 3" "pct5 0.05 5"; do
  set -- $v
  [ -f $w/var_$1/summary.json ] || sh src/variant.sh $1 $2 $3 > $w/var_$1.log 2>&1
  log "variant $1 done: $(grep RFI $w/var_$1.log)"
done

# stage 8: pack, validate, reports
python3 src/pack.py preflop $w/final/pre . > $w/pack.log
python3 src/pack.py postflop $w/final/ranges $w/final/pre_solves . >> $w/pack.log
python3 src/validate.py . $w/final/pre $w/final/ranges $w/final/pre_solves $w/var_rake0/pre > $w/validate.log 2>&1 || true
python3 src/rake_report.py rake_report.md $w/final/pre $w/var_rake0/pre "cap 3bb=$w/var_cap3/pre" "rake 5%=$w/var_pct5/pre" > $w/rake_report.log
log "stage 8 done"
echo PIPELINE_DONE >> work/pipeline.log
