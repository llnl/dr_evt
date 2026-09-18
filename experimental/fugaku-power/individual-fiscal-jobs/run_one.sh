#!/usr/bin/env bash

# Run exactly one capacity-generation, EASY, or EASYPower task. This helper is
# invoked by the generated Slurm files through its absolute shared-NFS path.

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../../.." && pwd)

usage() {
  cat <<'EOF'
Usage:
  run_one.sh capacity LABEL
  run_one.sh baseline LABEL
  run_one.sh easypower LABEL JOB_WINDOW TIME_WINDOW_SECONDS TIME_SLUG
EOF
}

if (($# < 2)); then
  usage >&2
  exit 2
fi

mode=$1
label=$2
if [[ ! $label =~ ^FY[0-9]{4}-H[12]$ ]]; then
  echo "invalid fiscal-half label: $label" >&2
  exit 2
fi

input_list="$script_dir/infile-lists/$label.txt"
historical_trace_dir=${HISTORICAL_TRACE_DIR:-$repo_dir/traces_nonoverlap}
output_root=${OUTPUT_ROOT:-$repo_dir/experimental/fugaku-power/results/individual-fiscal-sweep}
driver=${EASYPOWER_DRIVER:-$repo_dir/experimental/fugaku-power/build/easypower_experiment}
capacity_generator=${CAPACITY_GENERATOR:-$repo_dir/scripts/detect_queue_pause/generate_capacity_schedule.sh}
total_nodes=${TOTAL_NODES:-158976}
maximum_power=${MAXIMUM_POWER:-12000000}
initial_target=${INITIAL_TARGET:-$maximum_power}
trace_timezone=${TRACE_TIMEZONE:-JST}
overlap_policy=${OVERLAP_POLICY:-filename-month}
capacity_scenario=${CAPACITY_SCENARIO:-queue-pause-only}

if [[ ! -s $input_list ]]; then
  echo "input list is unavailable: $input_list" >&2
  exit 2
fi
if [[ ! -d $historical_trace_dir ]]; then
  echo "historical trace directory is unavailable: $historical_trace_dir" >&2
  exit 2
fi
if [[ ! $total_nodes =~ ^[0-9]+$ ]] || ((total_nodes <= 0)); then
  echo "TOTAL_NODES must be a positive integer" >&2
  exit 2
fi
case $capacity_scenario in
queue-pause-only | with-reduced) ;;
*)
  echo "CAPACITY_SCENARIO must be queue-pause-only or with-reduced" >&2
  exit 2
  ;;
esac

mkdir -p -- "$output_root"
output_root=$(cd -- "$output_root" && pwd)
historical_trace_dir=$(cd -- "$historical_trace_dir" && pwd)
half_root="$output_root/$label"
capacity_dir="$half_root/capacity-detection"
mkdir -p -- "$half_root"

if [[ $capacity_scenario == with-reduced ]]; then
  capacity_schedule="$capacity_dir/resource_capacity_with_reduced_capacity.csv"
else
  capacity_schedule="$capacity_dir/resource_capacity_without_reduced_capacity.csv"
fi

load_runtime() {
  module unload python/3.13.2 2>/dev/null || true
  module load gcc/13.3.1-magic openmpi/4.1.2 boost/1.86.0 cmake/3.30.5
  export OMP_NUM_THREADS=1
}

require_capacity_schedule() {
  if [[ ! -s $capacity_schedule || ! -f $capacity_dir/.complete ]]; then
    echo "capacity schedule is not complete: $capacity_schedule" >&2
    echo "run jobs/capacity/capacity-$label.slurm first" >&2
    exit 2
  fi
}

case $mode in
capacity)
  (($# == 2)) || { usage >&2; exit 2; }
  if [[ -f $capacity_dir/.complete && -s $capacity_schedule ]]; then
    echo "capacity schedule already complete: $capacity_schedule"
    exit 0
  fi
  if [[ -e $capacity_dir ]]; then
    echo "refusing to overwrite incomplete capacity directory: $capacity_dir" >&2
    exit 2
  fi
  if [[ ! -x $capacity_generator ]]; then
    echo "capacity generator is unavailable: $capacity_generator" >&2
    exit 2
  fi
  capacity_generator=$(realpath -- "$capacity_generator")

  generation_id=${SLURM_JOB_ID:-manual-$$}
  staging_dir="$half_root/capacity-detection.tmp-$generation_id"
  if [[ -e $staging_dir ]]; then
    echo "refusing to overwrite capacity staging directory: $staging_dir" >&2
    exit 2
  fi
  input_dir="$staging_dir/input"
  mkdir -p -- "$input_dir"
  while IFS= read -r simulation_trace; do
    [[ -n $simulation_trace ]] || continue
    historical_trace="$historical_trace_dir/$(basename -- "$simulation_trace")"
    if [[ ! -f $historical_trace ]]; then
      echo "$label is missing historical trace $historical_trace" >&2
      exit 1
    fi
    ln -s -- "$historical_trace" "$input_dir/$(basename -- "$historical_trace")"
  done <"$input_list"

  module load python/3.13.2
  CAPACITY_NODES="$total_nodes" TRACE_TIMEZONE="$trace_timezone" \
    OVERLAP_POLICY="$overlap_policy" SIMULATOR_FORMAT=1 \
    "$capacity_generator" \
    "$input_dir/*_scheduling_trace.csv" "$staging_dir" \
    >"$half_root/capacity-detection-$generation_id.log" 2>&1
  for generated_schedule in \
    "$staging_dir/resource_capacity_with_reduced_capacity.csv" \
    "$staging_dir/resource_capacity_without_reduced_capacity.csv"; do
    if [[ ! -s $generated_schedule ]]; then
      echo "capacity generator did not produce $generated_schedule" >&2
      exit 1
    fi
  done
  touch "$staging_dir/.complete"
  mv -- "$staging_dir" "$capacity_dir"
  echo "capacity schedule complete: $capacity_schedule"
  ;;

baseline)
  (($# == 2)) || { usage >&2; exit 2; }
  require_capacity_schedule
  if [[ ! -x $driver ]]; then
    echo "EASYPower driver is unavailable: $driver" >&2
    exit 2
  fi
  driver=$(realpath -- "$driver")
  final_dir="$half_root/easy-baseline"
  if [[ -f $final_dir/.complete && -s $final_dir/jobs.csv &&
        -s $final_dir/resources.csv ]]; then
    echo "EASY baseline already complete: $final_dir"
    exit 0
  fi
  if [[ -e $final_dir ]]; then
    echo "refusing to overwrite incomplete baseline directory: $final_dir" >&2
    exit 2
  fi
  run_id=${SLURM_JOB_ID:-manual-$$}
  staging_dir="$half_root/easy-baseline.tmp-$run_id"
  if [[ -e $staging_dir ]]; then
    echo "refusing to overwrite baseline staging directory: $staging_dir" >&2
    exit 2
  fi
  mkdir -- "$staging_dir"
  load_runtime
  cd -- "$repo_dir"
  "$driver" easy-progressive "$input_list" "$staging_dir" \
    "$total_nodes" 1 "$maximum_power" "$initial_target" 0 \
    "$capacity_schedule" >"$half_root/easy-baseline-$run_id.log" 2>&1
  if [[ ! -s $staging_dir/jobs.csv || ! -s $staging_dir/resources.csv ]]; then
    echo "EASY baseline did not produce complete output in $staging_dir" >&2
    exit 1
  fi
  touch "$staging_dir/.complete"
  mv -- "$staging_dir" "$final_dir"
  echo "EASY baseline complete: $final_dir"
  ;;

easypower)
  (($# == 5)) || { usage >&2; exit 2; }
  job_window=$3
  time_window=$4
  time_slug=$5
  if [[ ! $job_window =~ ^[0-9]+$ ]] || ((job_window <= 0)); then
    echo "JOB_WINDOW must be a positive integer" >&2
    exit 2
  fi
  if [[ ! $time_window =~ ^[0-9]+$ ]]; then
    echo "TIME_WINDOW_SECONDS must be a nonnegative integer" >&2
    exit 2
  fi
  if [[ ! $time_slug =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "invalid time slug: $time_slug" >&2
    exit 2
  fi
  require_capacity_schedule
  if [[ ! -x $driver ]]; then
    echo "EASYPower driver is unavailable: $driver" >&2
    exit 2
  fi
  driver=$(realpath -- "$driver")
  job_root="$half_root/job-window-$job_window/$label"
  final_dir="$job_root/easypower_n${job_window}_t${time_slug}"
  if [[ -f $final_dir/.complete && -s $final_dir/jobs.csv &&
        -s $final_dir/resources.csv && -s $final_dir/target_horizon.csv ]]; then
    echo "EASYPower run already complete: $final_dir"
    exit 0
  fi
  if [[ -e $final_dir ]]; then
    echo "refusing to overwrite incomplete EASYPower directory: $final_dir" >&2
    exit 2
  fi
  mkdir -p -- "$job_root"
  run_id=${SLURM_JOB_ID:-manual-$$}
  staging_dir="$job_root/easypower_n${job_window}_t${time_slug}.tmp-$run_id"
  if [[ -e $staging_dir ]]; then
    echo "refusing to overwrite EASYPower staging directory: $staging_dir" >&2
    exit 2
  fi
  mkdir -- "$staging_dir"
  load_runtime
  cd -- "$repo_dir"
  "$driver" easypower-progressive "$input_list" "$staging_dir" \
    "$total_nodes" "$job_window" "$maximum_power" "$initial_target" \
    "$time_window" "$capacity_schedule" \
    >"$job_root/easypower_n${job_window}_t${time_slug}-$run_id.log" 2>&1
  for output_file in jobs.csv resources.csv target_horizon.csv; do
    if [[ ! -s $staging_dir/$output_file ]]; then
      echo "EASYPower run did not produce $staging_dir/$output_file" >&2
      exit 1
    fi
  done
  touch "$staging_dir/.complete"
  mv -- "$staging_dir" "$final_dir"
  echo "EASYPower run complete: $final_dir"
  ;;

*)
  echo "unknown mode: $mode" >&2
  usage >&2
  exit 2
  ;;
esac
