#!/usr/bin/env bash

# Submit the fiscal-half sweep, monitor it, and collect successful results.

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../../.." && pwd)

trace_dir=${TRACE_DIR:-$repo_dir/traces_no_times_nonoverlap}
historical_trace_dir=${HISTORICAL_TRACE_DIR:-$repo_dir/traces_nonoverlap}
total_nodes=${TOTAL_NODES:-158976}
output_root=${OUTPUT_ROOT:-$repo_dir/experimental/fugaku-power/results/fiscal-sweeps}
driver=${EASYPOWER_DRIVER:-$repo_dir/experimental/fugaku-power/build/easypower_experiment}
poll_seconds=${POLL_INTERVAL_SECONDS:-600}
collect=1

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Submit the 35-node fiscal-half sweep, poll its Slurm status, and collect its
result tables after successful completion.

Options:
  --trace-dir PATH             Simulation traces (default: $trace_dir)
  --historical-trace-dir PATH  Traces containing begin/end times
  --total-nodes COUNT          Simulated system capacity (default: $total_nodes)
  --output-root PATH           Parent result directory (default: $output_root)
  --driver PATH                Prebuilt private executable (default: $driver)
  --interval SECONDS           Poll interval (default: $poll_seconds)
  --no-collect                 Monitor without collecting tables
  -h, --help                   Show this help
EOF
}

while (($#)); do
  case $1 in
  --trace-dir)
    (($# >= 2)) || { echo "--trace-dir requires a path" >&2; exit 2; }
    trace_dir=$2
    shift 2
    ;;
  --historical-trace-dir)
    (($# >= 2)) || {
      echo "--historical-trace-dir requires a path" >&2
      exit 2
    }
    historical_trace_dir=$2
    shift 2
    ;;
  --total-nodes)
    (($# >= 2)) || { echo "--total-nodes requires a value" >&2; exit 2; }
    total_nodes=$2
    shift 2
    ;;
  --output-root)
    (($# >= 2)) || { echo "--output-root requires a path" >&2; exit 2; }
    output_root=$2
    shift 2
    ;;
  --driver)
    (($# >= 2)) || { echo "--driver requires a path" >&2; exit 2; }
    driver=$2
    shift 2
    ;;
  --interval)
    (($# >= 2)) || { echo "--interval requires a value" >&2; exit 2; }
    poll_seconds=$2
    shift 2
    ;;
  --no-collect)
    collect=0
    shift
    ;;
  -h | --help)
    usage
    exit 0
    ;;
  *)
    echo "unknown option: $1" >&2
    usage >&2
    exit 2
    ;;
  esac
done

if [[ ! -d $trace_dir ]]; then
  echo "trace directory not found: $trace_dir" >&2
  exit 2
fi
if [[ ! -d $historical_trace_dir ]]; then
  echo "historical trace directory not found: $historical_trace_dir" >&2
  exit 2
fi
if [[ ! $total_nodes =~ ^[0-9]+$ ]] || ((total_nodes <= 0)); then
  echo "--total-nodes must be a positive integer" >&2
  exit 2
fi
if [[ ! $poll_seconds =~ ^[0-9]+$ ]] || ((poll_seconds <= 0)); then
  echo "--interval must be a positive integer number of seconds" >&2
  exit 2
fi
if [[ ! -x $driver ]]; then
  echo "EASYPower driver is not executable: $driver" >&2
  echo "build it with $script_dir/build_easypower_experiment.sh" >&2
  exit 2
fi
if ! command -v sbatch >/dev/null 2>&1; then
  echo "sbatch is unavailable; run this script on a Slurm login node" >&2
  exit 2
fi

mkdir -p -- "$output_root"
trace_dir=$(cd -- "$trace_dir" && pwd)
historical_trace_dir=$(cd -- "$historical_trace_dir" && pwd)
output_root=$(cd -- "$output_root" && pwd)
driver=$(realpath -- "$driver")

export TRACE_DIR=$trace_dir
export HISTORICAL_TRACE_DIR=$historical_trace_dir
export TOTAL_NODES=$total_nodes
export OUTPUT_ROOT=$output_root
export BUILD_DRIVER=0
export EASYPOWER_DRIVER=$driver
export EASYPOWER_SCRIPT_DIR=$script_dir
export EASYPOWER_REPO_DIR=$repo_dir
export CAPACITY_GENERATOR=$repo_dir/scripts/detect_queue_pause/generate_capacity_schedule.sh
export SWEEP_RUNNER=$script_dir/run_easypower_sweep.py

submission=$(sbatch --parsable --export=ALL \
  "$script_dir/submit_easypower_fiscal_halves.slurm")
job_id=${submission%%;*}
if [[ ! $job_id =~ ^[0-9]+$ ]]; then
  echo "could not parse Slurm job ID from: $submission" >&2
  exit 1
fi

echo "Submitted fiscal sweep as job $job_id"
monitor_args=(
  "$job_id"
  --interval "$poll_seconds"
  --output-root "$output_root"
)
if ((collect)); then
  monitor_args+=(--collect)
fi
exec "$script_dir/monitor_easypower_fiscal_job.sh" "${monitor_args[@]}"
