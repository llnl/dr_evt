#!/usr/bin/env bash
set -euo pipefail
gcc -O2 -DVALIDATION_STATE_CACHE -I/workspace/slurm-modern-build -I/workspace/slurm-modern-source \
    /dr_evt_source/validation/slurm/licenses/client.c -o /workspace/slurm-event/client_state \
    -L/workspace/slurm-modern-build/src/api/.libs -lslurmfull -L/workspace/slurm-event -lvalidation_clock \
    -Wl,-rpath,/workspace/slurm-modern-build/src/api/.libs -Wl,-rpath,/workspace/slurm-event
