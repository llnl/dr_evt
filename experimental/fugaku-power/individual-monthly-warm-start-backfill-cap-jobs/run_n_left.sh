#!/usr/bin/env bash
set -euo pipefail

# Number of unfinished jobs to submit. The first argument overrides this default.
n=${1:-120}
manifest_kind=easypower-cap
output_name=individual-monthly-warm-start-backfill-cap-sweep

suite_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$suite_dir/../scripts/submit_monthly_warm_start_cap_jobs.sh"
