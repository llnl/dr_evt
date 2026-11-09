#!/usr/bin/env bash
set -euo pipefail
validation_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd "$validation_dir/../.." && pwd)
results=${OPENPBS_VALIDATION_RESULTS:-"$validation_dir/results/run-$(date -u +%Y%m%dT%H%M%SZ)"}
mkdir -p "$results"
docker build -t dr-evt-openpbs-validation "$validation_dir"
docker run --rm \
    --mount "type=bind,src=$repo_dir,dst=/dr_evt_source,readonly" \
    --mount "type=bind,src=$results,dst=/results" \
    --mount "type=volume,src=${OPENPBS_VALIDATION_BUILD_VOLUME:-dr-evt-openpbs-build},dst=/workspace" \
    -e "OPENPBS_VALIDATION_MAX_JOBS=${OPENPBS_VALIDATION_MAX_JOBS:-2000}" \
    -e DR_EVT_VALIDATION_WORKING_TREE=1 \
    dr-evt-openpbs-validation bash /dr_evt_source/validation/openpbs/build-and-test.sh "$@"
