#!/usr/bin/env bash
set -euo pipefail
validation_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd "$validation_dir/../../.." && pwd)
results=${SLURM_LICENSE_RESULTS:-"$repo_dir/validation/slurm/results/licenses/run-$(date -u +%Y%m%dT%H%M%SZ)"}
mkdir -p "$results"
docker run --rm --hostname localhost \
    --mount "type=bind,src=$repo_dir/external/slurm,dst=/slurm-modern-source,readonly" \
    --mount "type=bind,src=$repo_dir,dst=/dr_evt_source,readonly" \
    --mount "type=bind,src=$results,dst=/results" \
    --mount "type=volume,src=dr-evt-slurm-license-build,dst=/workspace" \
    -e "SLURM_LICENSE_SKIP_BUILD=${SLURM_LICENSE_SKIP_BUILD:-0}" \
    -e "SLURM_LICENSE_SELECT_TYPE=${SLURM_LICENSE_SELECT_TYPE:-select/cons_tres}" \
    --entrypoint bash dr-evt-slurm-validation -c \
    'if test "$SLURM_LICENSE_SKIP_BUILD" != 1; then bash /dr_evt_source/validation/slurm/licenses/build.sh; fi
     python3 /dr_evt_source/validation/slurm/licenses/compare.py'
