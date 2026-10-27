#!/bin/bash
# Full pipeline. Each stage is resumable from work/ (stages skip finished parts).
set -e
cd "$(dirname "$0")"
python3 stage1_classes.py
python3 stage2_sim.py
python3 stage3_model.py
python3 stage4_calibrate.py
python3 stage5_iterate.py
python3 stage6_borderline.py
python3 stage6b_topup.py
python3 stage7_pack.py
python3 validate_decode.py
python3 stage8_checks.py
python3 stage8_report.py
