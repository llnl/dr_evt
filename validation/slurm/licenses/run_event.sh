#!/usr/bin/env bash
set -euo pipefail
validation_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd "$validation_dir/../../.." && pwd)
results=${SLURM_LICENSE_RESULTS:-"$repo_dir/validation/slurm/results/licenses/run-$(date -u +%Y%m%dT%H%M%SZ)"}
mkdir -p "$results"
docker run --rm --hostname localhost \
    --mount "type=bind,src=$repo_dir,dst=/dr_evt_source,readonly" \
    --mount "type=bind,src=$repo_dir/external/slurm,dst=/slurm-modern-source,readonly" \
    --mount "type=bind,src=$results,dst=/results" \
    --mount "type=volume,src=dr-evt-slurm-license-build,dst=/workspace" \
    --entrypoint bash dr-evt-slurm-validation -c \
    'bash /dr_evt_source/validation/slurm/licenses/build.sh &&
     bash /dr_evt_source/validation/slurm/licenses/build_event.sh &&
     bash /dr_evt_source/validation/slurm/licenses/build_state_client.sh &&
     export SLURM_LICENSE_STATE_ONLY=1 &&
     python3 /dr_evt_source/validation/slurm/licenses/event_trace.py --outdir /results/controls &&
     python3 /dr_evt_source/validation/slurm/licenses/event_trace.py --outdir /results/paired --trace /dr_evt_source/lassen_traces/lassen-756-01.csv &&
     python3 /dr_evt_source/validation/slurm/licenses/audit_event.py /results/paired/lassen-prefix2000-hybrid-r32-q64 &&
     python3 /dr_evt_source/validation/slurm/licenses/event_trace.py --bf-licenses off --outdir /results/paired-license-off --trace /dr_evt_source/lassen_traces/lassen-756-01.csv &&
     python3 /dr_evt_source/validation/slurm/licenses/audit_event.py /results/paired-license-off/lassen-prefix2000-hybrid-r32-q64 &&
     python3 /dr_evt_source/validation/slurm/licenses/summarize_licenses.py /results/paired/lassen-prefix2000-hybrid-r32-q64 /results/paired-license-off/lassen-prefix2000-hybrid-r32-q64 /results'
