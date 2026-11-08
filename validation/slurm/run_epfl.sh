#!/usr/bin/env bash
set -euo pipefail
validation_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd "$validation_dir/../.." && pwd)
results=${SLURM_VALIDATION_RESULTS:-"$validation_dir/results/run-$(date -u +%Y%m%dT%H%M%SZ)"}
image=${SLURM_VALIDATION_IMAGE:-dr-evt-slurm-validation}
volume=${SLURM_VALIDATION_BUILD_VOLUME:-dr-evt-slurm-validation-build}
test -d "$repo_dir/external/slurm_simulator/.git"
mkdir -p "$results"
docker build -t "$image" "$validation_dir"
docker run --rm \
    --mount "type=bind,src=$repo_dir/external/slurm_simulator,dst=/slurm-source,readonly" \
    --mount "type=bind,src=$repo_dir,dst=/dr_evt_source,readonly" \
    --mount "type=bind,src=$results,dst=/results" \
    --mount "type=volume,src=$volume,dst=/workspace" \
    -e "BUILD_JOBS=${BUILD_JOBS:-4}" \
    -e "SLURM_VALIDATION_TRACE=${SLURM_VALIDATION_TRACE:-}" "$image"
