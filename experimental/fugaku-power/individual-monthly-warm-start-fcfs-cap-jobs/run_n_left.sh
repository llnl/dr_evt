#!/usr/bin/env bash
set -euo pipefail

# Number of unfinished jobs to submit. The first argument overrides this default.
n=${1:-36}
max_n=60
manifest_kind=easypower-fcfs-cap
output_name=individual-monthly-warm-start-fcfs-cap-sweep
years=${YEARS:-}

suite_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$suite_dir/../scripts/submit_monthly_warm_start_cap_jobs.sh"
