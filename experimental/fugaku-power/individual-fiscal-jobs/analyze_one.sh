#!/usr/bin/env bash

# Analyze one fiscal-half/job-window after its baseline and three candidates
# have completed. This performs no simulation and requires no Slurm allocation.

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../../.." && pwd)

if (($# != 2)); then
  echo "Usage: analyze_one.sh LABEL JOB_WINDOW" >&2
  exit 2
fi
label=$1
job_window=$2
if [[ ! $label =~ ^FY[0-9]{4}-H[12]$ || ! $job_window =~ ^[0-9]+$ ]]; then
  echo "invalid label or job window" >&2
  exit 2
fi

output_root=${OUTPUT_ROOT:-$repo_dir/experimental/fugaku-power/results/individual-fiscal-sweep}
total_nodes=${TOTAL_NODES:-158976}
maximum_power=${MAXIMUM_POWER:-12000000}
initial_target=${INITIAL_TARGET:-$maximum_power}
capacity_scenario=${CAPACITY_SCENARIO:-queue-pause-only}
half_root="$output_root/$label"
capacity_dir="$half_root/capacity-detection"
if [[ $capacity_scenario == with-reduced ]]; then
  capacity_schedule="$capacity_dir/resource_capacity_with_reduced_capacity.csv"
else
  capacity_schedule="$capacity_dir/resource_capacity_without_reduced_capacity.csv"
fi

module load python/3.13.2
python3 "$repo_dir/experimental/fugaku-power/scripts/run_easypower_sweep.py" \
  --infile-list "$script_dir/infile-lists/$label.txt" \
  --total-nodes "$total_nodes" \
  --candidate-job-windows "$job_window" \
  --candidate-time-windows unlimited,1h,6h \
  --maximum-power "$maximum_power" \
  --initial-target "$initial_target" \
  --endpoint-windows 1m,5m,15m \
  --capacity-schedule "$capacity_schedule" \
  --shared-baseline-dir "$half_root/easy-baseline" \
  --output-dir "$half_root/job-window-$job_window" \
  --analysis-only
