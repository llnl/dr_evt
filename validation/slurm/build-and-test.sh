#!/usr/bin/env bash
set -euo pipefail
bash /validation-slurm/build.sh
python3 /validation-slurm/test_audit.py
mkdir -p /run/munge
chown munge:munge /run/munge
runuser -u munge -- munged
options=(--outdir /results/paired)
if test -n "${SLURM_VALIDATION_TRACE:-}"; then
    options+=(--trace "$SLURM_VALIDATION_TRACE")
fi
python3 /validation-slurm/compare.py "${options[@]}" | tee /results/paired.log
if test -n "${SLURM_VALIDATION_TRACE:-}"; then
    python3 /validation-slurm/plot.py /results/paired/lassen-prefix2000-hybrid-r32-q64/report.json
fi
