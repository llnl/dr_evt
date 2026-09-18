#!/usr/bin/env bash

# Poll one fiscal-sweep Slurm allocation until it reaches a terminal state.
# A "job ID" is the number printed by sbatch, for example 845219.

module load gcc/13.3.1-magic openmpi/4.1.2 cmake/3.30.5 python/3.13.
export BUILD_DRIVER=0
export EASYPOWER_BUILD_DIR=/p/vast1/f-data/build

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../../.." && pwd)

usage() {
  cat <<'EOF'
Usage: monitor_easypower_fiscal_job.sh JOB_ID [options]

Options:
  --interval SECONDS  Poll interval (default: 600 seconds / 10 minutes)
  --output-root PATH  Sweep output root used at submission time
  --collect           Collect result tables after successful completion
  -h, --help          Show this help
EOF
}

if (($# == 0)); then
  usage >&2
  exit 2
fi
if [[ $1 == -h || $1 == --help ]]; then
  usage
  exit 0
fi

job_id=$1
shift
poll_seconds=${POLL_INTERVAL_SECONDS:-600}
output_root=${OUTPUT_ROOT:-$repo_dir/easypower-fiscal-sweeps}
collect=0

while (($#)); do
  case $1 in
  --interval)
    (($# >= 2)) || { echo "--interval requires a value" >&2; exit 2; }
    poll_seconds=$2
    shift 2
    ;;
  --output-root)
    (($# >= 2)) || { echo "--output-root requires a path" >&2; exit 2; }
    output_root=$2
    shift 2
    ;;
  --collect)
    collect=1
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

if [[ ! $job_id =~ ^[0-9]+$ ]]; then
  echo "JOB_ID must be the numeric ID printed by sbatch" >&2
  exit 2
fi
if [[ ! $poll_seconds =~ ^[0-9]+$ ]] || ((poll_seconds <= 0)); then
  echo "--interval must be a positive integer number of seconds" >&2
  exit 2
fi
if ! command -v squeue >/dev/null 2>&1; then
  echo "squeue is unavailable; run this script on a Slurm login node" >&2
  exit 2
fi
if ! command -v sacct >/dev/null 2>&1; then
  echo "sacct is unavailable; run this script on a Slurm login node" >&2
  exit 2
fi

missing_polls=0
while true; do
  queue_state=$(squeue --noheader --jobs "$job_id" --format='%T' |
    awk 'NF {print $1; exit}')
  if [[ -n $queue_state ]]; then
    printf '[%s] job %s: %s\n' "$(date '+%Y-%m-%d %H:%M:%S %Z')" \
      "$job_id" "$queue_state"
    missing_polls=0
    sleep "$poll_seconds"
    continue
  fi

  accounting_state=$(sacct --noheader --parsable2 --allocations \
    --jobs "$job_id" --format=State |
    awk -F '|' 'NF && $1 != "" {print $1; exit}')
  accounting_state=${accounting_state%%+*}
  accounting_state=${accounting_state%% *}

  if [[ -z $accounting_state ]]; then
    missing_polls=$((missing_polls + 1))
    if ((missing_polls >= 3)); then
      echo "job $job_id was not found by squeue or sacct" >&2
      exit 2
    fi
    printf '[%s] job %s: awaiting accounting record\n' \
      "$(date '+%Y-%m-%d %H:%M:%S %Z')" "$job_id"
    sleep "$poll_seconds"
    continue
  fi

  printf '[%s] job %s: %s\n' "$(date '+%Y-%m-%d %H:%M:%S %Z')" \
    "$job_id" "$accounting_state"
  case $accounting_state in
  COMPLETED)
    run_directory="$output_root/run-$job_id"
    if ((collect)); then
      "$script_dir/collect_easypower_fiscal_results.py" "$run_directory"
    else
      echo "Results: $run_directory"
      echo "Collect with:"
      printf '  %q %q\n' \
        "$script_dir/collect_easypower_fiscal_results.py" "$run_directory"
    fi
    exit 0
    ;;
  PENDING | RUNNING | CONFIGURING | COMPLETING | SUSPENDED | RESIZING)
    sleep "$poll_seconds"
    ;;
  *)
    echo "job $job_id did not complete successfully" >&2
    exit 1
    ;;
  esac
done
