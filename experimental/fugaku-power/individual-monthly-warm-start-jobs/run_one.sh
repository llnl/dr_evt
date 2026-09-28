#!/usr/bin/env bash

# Run one monthly capacity, EASY, or EASYPower task.  A monthly simulation
# input is built beneath OUTPUT_ROOT and includes explicit initialization jobs
# for every historical job still running at that month's first instant.

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../../.." && pwd)
usage() { echo "Usage: run_one.sh {capacity|baseline|easypower} YYYY-MM [JOB_WINDOW TIME_SECONDS TIME_SLUG]" >&2; }
if (($# < 2)); then usage; exit 2; fi
mode=$1
label=$2
[[ $label =~ ^20[0-9]{2}-(0[1-9]|1[0-2])$ ]] || { echo "invalid month: $label" >&2; exit 2; }

historical_trace_dir=${HISTORICAL_TRACE_DIR:-$repo_dir/traces_nonoverlap}
workload_trace_dir=${WORKLOAD_TRACE_DIR:-$repo_dir/traces_no_times_nonoverlap}
output_root=${OUTPUT_ROOT:-$repo_dir/experimental/fugaku-power/results/individual-monthly-warm-start-sweep}
shared_output_root=${SHARED_OUTPUT_ROOT:-$output_root}
driver=${EASYPOWER_DRIVER:-$repo_dir/experimental/fugaku-power/build/easypower_experiment}
capacity_generator=${CAPACITY_GENERATOR:-$repo_dir/scripts/detect_queue_pause/generate_capacity_schedule.sh}
total_nodes=${TOTAL_NODES:-158976}
maximum_power=${MAXIMUM_POWER:-12000000}
initial_target=${INITIAL_TARGET:-$maximum_power}
trace_timezone=${TRACE_TIMEZONE:-JST}
overlap_policy=${OVERLAP_POLICY:-filename-month}
capacity_scenario=${CAPACITY_SCENARIO:-queue-pause-only}
cap_backfill_power=${CAP_BACKFILL_POWER:-0}
cap_fcfs_power=${CAP_FCFS_POWER:-0}

[[ -d $historical_trace_dir && -d $workload_trace_dir ]] || { echo "trace directory unavailable" >&2; exit 2; }
[[ $total_nodes =~ ^[0-9]+$ ]] && ((total_nodes > 0)) || { echo "TOTAL_NODES must be positive" >&2; exit 2; }
case $capacity_scenario in queue-pause-only|with-reduced) ;; *) echo "invalid CAPACITY_SCENARIO" >&2; exit 2;; esac
case $cap_backfill_power in 0|1) ;; *) echo "CAP_BACKFILL_POWER must be 0 or 1" >&2; exit 2;; esac
case $cap_fcfs_power in 0|1) ;; *) echo "CAP_FCFS_POWER must be 0 or 1" >&2; exit 2;; esac
mkdir -p -- "$output_root"
output_root=$(cd -- "$output_root" && pwd)
[[ -d $shared_output_root ]] || { echo "shared result root unavailable: $shared_output_root" >&2; exit 2; }
shared_output_root=$(cd -- "$shared_output_root" && pwd)
month_root="$output_root/$label"
shared_month_root="$shared_output_root/$label"
capacity_dir="$shared_month_root/capacity-detection"
input_dir="$shared_month_root/input"
monthly_input="$input_dir/warm-start-scheduling_trace.csv"
monthly_list="$input_dir/infile.txt"
month_slug="${label:2:2}_${label:5:2}"
capacity_history="$input_dir/${month_slug}_scheduling_trace.csv"
if [[ $capacity_scenario == with-reduced ]]; then capacity_schedule="$capacity_dir/resource_capacity_with_reduced_capacity.csv"; else capacity_schedule="$capacity_dir/resource_capacity_without_reduced_capacity.csv"; fi

load_runtime() {
  module unload python/3.13.2 2>/dev/null || true
  module load gcc/13.3.1-magic openmpi/4.1.2 boost/1.86.0 cmake/3.30.5
  export OMP_NUM_THREADS=1
}
month_end_epoch() {
  module load python/3.13.2
  python3 -c '
from datetime import datetime
from zoneinfo import ZoneInfo
import sys
if sys.argv[3]:
    value = sys.argv[3]
    try:
        print("{:.15g}".format(float(value)))
    except ValueError:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=ZoneInfo("Asia/Tokyo" if sys.argv[2] == "JST" else sys.argv[2]))
        print("{:.15g}".format(parsed.timestamp()))
    raise SystemExit
year, month = map(int, sys.argv[1].split("-"))
timezone = "Asia/Tokyo" if sys.argv[2] == "JST" else sys.argv[2]
if month == 12:
    year, month = year + 1, 1
else:
    month += 1
print("{:.15g}".format(datetime(year, month, 1, tzinfo=ZoneInfo(timezone)).timestamp()))
' "$label" "$trace_timezone" "${SIM_END_TIME:-}"
}
month_start_epoch() {
  module load python/3.13.2
  python3 -c '
from datetime import datetime
from zoneinfo import ZoneInfo
import sys
timezone = "Asia/Tokyo" if sys.argv[2] == "JST" else sys.argv[2]
value = sys.argv[3]
if value:
    try:
        print("{:.15g}".format(float(value)))
    except ValueError:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=ZoneInfo(timezone))
        print("{:.15g}".format(parsed.timestamp()))
else:
    year, month = map(int, sys.argv[1].split("-"))
    print("{:.15g}".format(datetime(year, month, 1, tzinfo=ZoneInfo(timezone)).timestamp()))
' "$label" "$trace_timezone" "${SIM_START_TIME:-}"
}
require_capacity() {
  [[ -s $capacity_schedule && -f $capacity_dir/.complete ]] || { echo "capacity schedule is not complete; run jobs/capacity/capacity-$label.slurm first" >&2; exit 2; }
}
require_shared_baseline() {
  [[ $shared_output_root == "$output_root" ]] && return
  local baseline="$shared_month_root/easy-baseline"
  [[ -f $baseline/.complete && -s $baseline/jobs.csv && -s $baseline/resources.csv ]] || {
    echo "shared baseline is not complete: $baseline" >&2
    exit 2
  }
}
prepare_input() {
  if [[ -s $monthly_input && -s $monthly_list && -s $capacity_history ]]; then return; fi
  [[ ! -e $input_dir ]] || { echo "refusing to overwrite incomplete input directory: $input_dir" >&2; exit 2; }
  local staging="$month_root/input.tmp-${SLURM_JOB_ID:-manual-$$}"
  [[ ! -e $staging ]] || { echo "input staging directory already exists: $staging" >&2; exit 2; }
  mkdir -p -- "$staging"
  module load python/3.13.2
  prepare_args=(--historical-dir "$historical_trace_dir" --workload-dir "$workload_trace_dir" \
                --timezone "$trace_timezone" --total-nodes "$total_nodes" \
                --capacity-history "$staging/${month_slug}_scheduling_trace.csv")
  [[ -n ${SIM_START_TIME:-} ]] && prepare_args+=(--start-time "$SIM_START_TIME")
  [[ -n ${SIM_END_TIME:-} ]] && prepare_args+=(--end-time "$SIM_END_TIME")
  python3 "$script_dir/prepare_monthly_input.py" "$label" "$staging/warm-start-scheduling_trace.csv" \
    "${prepare_args[@]}" \
    >"$month_root/input-${SLURM_JOB_ID:-manual-$$}.log"
  printf '%s\n' "$input_dir/warm-start-scheduling_trace.csv" >"$staging/infile.txt"
  mv -- "$staging" "$input_dir"
}

case $mode in
capacity)
  (($# == 2)) || { usage; exit 2; }
  [[ -f $capacity_dir/.complete && -s $capacity_schedule ]] && { echo "capacity schedule already complete"; exit 0; }
  [[ ! -e $capacity_dir ]] || { echo "refusing to overwrite incomplete capacity directory: $capacity_dir" >&2; exit 2; }
  [[ -x $capacity_generator ]] || { echo "capacity generator unavailable" >&2; exit 2; }
  prepare_input
  analysis_start=$(month_start_epoch)
  analysis_end=$(month_end_epoch)
  staging="$month_root/capacity-detection.tmp-${SLURM_JOB_ID:-manual-$$}"
  [[ ! -e $staging ]] || { echo "capacity staging directory already exists" >&2; exit 2; }
  mkdir -p -- "$staging/input"
  ln -s -- "$capacity_history" "$staging/input/${month_slug}_scheduling_trace.csv"
  module load python/3.13.2
  CAPACITY_NODES="$total_nodes" TRACE_TIMEZONE="$trace_timezone" OVERLAP_POLICY="$overlap_policy" \
    ANALYSIS_START="$analysis_start" ANALYSIS_END="$analysis_end" SIMULATOR_FORMAT=1 \
    "$capacity_generator" "$staging/input/*_scheduling_trace.csv" "$staging" >"$month_root/capacity-${SLURM_JOB_ID:-manual-$$}.log" 2>&1
  [[ -s $staging/resource_capacity_with_reduced_capacity.csv && -s $staging/resource_capacity_without_reduced_capacity.csv ]] || { echo "capacity generator did not produce schedules" >&2; exit 1; }
  touch "$staging/.complete"; mv -- "$staging" "$capacity_dir"
  ;;
baseline)
  (($# == 2)) || { usage; exit 2; }; require_capacity; prepare_input
  [[ -x $driver ]] || { echo "EASYPower driver unavailable: $driver" >&2; exit 2; }
  final="$month_root/easy-baseline"; [[ -f $final/.complete && -s $final/jobs.csv && -s $final/resources.csv ]] && { echo "baseline already complete"; exit 0; }; [[ ! -e $final ]] || { echo "refusing to overwrite incomplete baseline" >&2; exit 2; }
  cutoff=$(month_end_epoch)
  staging="$month_root/easy-baseline.tmp-${SLURM_JOB_ID:-manual-$$}"; mkdir -- "$staging"; load_runtime; cd -- "$repo_dir"
  "$driver" easy-progressive "$monthly_list" "$staging" "$total_nodes" 1 "$maximum_power" "$initial_target" 0 "$capacity_schedule" "$cutoff" >"$month_root/baseline-${SLURM_JOB_ID:-manual-$$}.log" 2>&1
  [[ -s $staging/jobs.csv && -s $staging/resources.csv ]] || { echo "baseline output incomplete" >&2; exit 1; }; touch "$staging/.complete"; mv -- "$staging" "$final"
  ;;
easypower)
  (($# == 5)) || { usage; exit 2; }; window=$3; seconds=$4; slug=$5
  [[ $window =~ ^[1-9][0-9]*$ && $seconds =~ ^[0-9]+$ && $slug =~ ^[A-Za-z0-9._-]+$ ]] || { echo "invalid EASYPower arguments" >&2; exit 2; }
  require_capacity; require_shared_baseline; prepare_input; [[ -x $driver ]] || { echo "EASYPower driver unavailable: $driver" >&2; exit 2; }
  job_root="$month_root/job-window-$window/$label"; final="$job_root/easypower_n${window}_t${slug}"; [[ -f $final/.complete && -s $final/jobs.csv && -s $final/resources.csv && -s $final/target_horizon.csv ]] && { echo "EASYPower run already complete"; exit 0; }; [[ ! -e $final ]] || { echo "refusing to overwrite incomplete EASYPower result" >&2; exit 2; }
  cutoff=$(month_end_epoch)
  mkdir -p -- "$job_root"; staging="$job_root/easypower_n${window}_t${slug}.tmp-${SLURM_JOB_ID:-manual-$$}"; mkdir -- "$staging"; load_runtime; cd -- "$repo_dir"
  cap_args=()
  ((cap_backfill_power == 1)) && cap_args+=(--cap_backfill_power)
  ((cap_fcfs_power == 1)) && cap_args+=(--cap_fcfs_power)
  "$driver" easypower-progressive "$monthly_list" "$staging" "$total_nodes" "$window" "$maximum_power" "$initial_target" "$seconds" "$capacity_schedule" "$cutoff" "${cap_args[@]}" >"$job_root/easypower_n${window}_t${slug}-${SLURM_JOB_ID:-manual-$$}.log" 2>&1
  [[ -s $staging/jobs.csv && -s $staging/resources.csv && -s $staging/target_horizon.csv ]] || { echo "EASYPower output incomplete" >&2; exit 1; }; touch "$staging/.complete"; mv -- "$staging" "$final"
  ;;
*) usage; exit 2;;
esac
