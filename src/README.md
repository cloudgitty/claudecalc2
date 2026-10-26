# PLO6 6-max 200bb preflop ranges: reproduction

Requirements: Linux, gcc with OpenMP, Python 3.10+ with `numpy scipy lightgbm scikit-learn`
(`pip install numpy scipy lightgbm scikit-learn`). The C engine (`engine.c`) is compiled
automatically into `src/libplo6.so` on first import of `engine.py`.

Run everything (about 4 to 5 hours on 4 cores; every stage resumes from `work/`):

```bash
cd src
./run_pipeline.sh
```

or stage by stage:

| command | stage | output (under `work/` unless stated) |
|---|---|---|
| `python3 stage1_classes.py` | class list, combos, evaluator self-test | `classes.npz` |
| `python3 stage2_sim.py` | strength pilot, 12 reference bands, equity of all 962,988 classes vs every band (4,000 boards) | `order.npz`, `bands.npz`, `sim_bandNN.npz` |
| `python3 stage3_model.py` | features (`features.py`), LightGBM feature model (5-fold cross-fit), empirical-Bayes shrinkage, precision holdout | `eqm_bandNN.npz`, `model_stats.json` |
| `python3 stage4_calibrate.py` | R0 calibration (flop-turn-river simulations) | `r0.json` |
| `python3 stage5_iterate.py` | damped best-response iteration | `state_main.npz`, `hist_main.json` |
| `python3 stage6_borderline.py` | borderline re-simulation vs the actual ranges, refinement rounds | `resim.npz`, `corr.pkl`, `state_final.npz` |
| `python3 stage7_pack.py` | packing and structural validation | `../plo6_ranges_packed.json`, `../widths.json`, `../test_vectors.json` |
| `python3 validate_decode.py` | independent decoder round trip | `validation_decode.json` |
| `python3 stage8_checks.py` | sensitivity reruns, hand sanity, first-principles checks | `checks.json` |
| `python3 stage8_report.py` | report | `../REPORT.md` |

Files: `engine.c` (evaluator, class codes, Monte Carlo equity, postflop simulator),
`engine.py` (ctypes wrapper), `features.py` (hand features), `solve.py` (game tree,
EV model, ramps, iteration), `common.py` (paths, progress log).

All random streams are seeded, so a rerun on the same machine reproduces the outputs.
The thread count changes only the scheduling, not the random streams, because each
task seeds its own generator.
