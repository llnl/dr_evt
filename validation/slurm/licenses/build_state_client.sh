#!/usr/bin/env bash
# Build the cached-state Slurm validation client from a DR_EVT source checkout.
# DR_EVT_SOURCE_ROOT defaults to the image copy and may name a mounted checkout.
set -euo pipefail
DR_EVT_SOURCE_ROOT=${DR_EVT_SOURCE_ROOT:-/dr_evt_source}
gcc -O2 -DVALIDATION_STATE_CACHE -I/workspace/slurm-modern-build -I/workspace/slurm-modern-source \
    "$DR_EVT_SOURCE_ROOT/validation/slurm/licenses/client.c" -o /workspace/slurm-event/client_state \
    -L/workspace/slurm-modern-build/src/api/.libs -lslurmfull -L/workspace/slurm-event -lvalidation_clock \
    -Wl,-rpath,/workspace/slurm-modern-build/src/api/.libs -Wl,-rpath,/workspace/slurm-event
