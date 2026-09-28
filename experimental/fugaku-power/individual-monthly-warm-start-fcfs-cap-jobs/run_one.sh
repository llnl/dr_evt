#!/usr/bin/env bash

# Run the shared monthly warm-start workflow with strict power admission for
# FCFS-prefix jobs. Backfill admission remains uncapped.

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../../.." && pwd)
base_runner=${MONTHLY_WARM_START_RUNNER:-$repo_dir/experimental/fugaku-power/individual-monthly-warm-start-jobs/run_one.sh}

export CAP_BACKFILL_POWER=0
export CAP_FCFS_POWER=1
export OUTPUT_ROOT=${OUTPUT_ROOT:-$repo_dir/experimental/fugaku-power/results/individual-monthly-warm-start-fcfs-cap-sweep}
export SHARED_OUTPUT_ROOT=${SHARED_OUTPUT_ROOT:-$repo_dir/experimental/fugaku-power/results/individual-monthly-warm-start-sweep}

if [[ ${1:-} != easypower ]]; then
  echo "this cap suite reuses capacity and baseline results from SHARED_OUTPUT_ROOT; only easypower mode is allowed" >&2
  exit 2
fi

exec "$base_runner" "$@"
