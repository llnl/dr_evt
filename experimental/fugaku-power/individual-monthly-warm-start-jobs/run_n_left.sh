#!/usr/bin/env bash
set -euo pipefail

# Submit unfinished EASYPower jobs from this suite's manifest. Capacity and
# baseline jobs are submitted by hand (see README); this wrapper submits only
# `easypower` rows whose month already has complete capacity, baseline, and
# input results under the shared root, which for this suite is its own root.
# Number of unfinished jobs to submit. The first argument overrides this default.
n=${1:-12}
manifest_kind=easypower
output_name=individual-monthly-warm-start-sweep

suite_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# This suite produces the shared capacity, baseline, and input results itself,
# so its prerequisites live under its own output root unless overridden.
SHARED_OUTPUT_ROOT=${SHARED_OUTPUT_ROOT:-${OUTPUT_ROOT:-$suite_dir/../results/$output_name}}
source "$suite_dir/../scripts/submit_monthly_warm_start_cap_jobs.sh"
