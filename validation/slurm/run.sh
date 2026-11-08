#!/usr/bin/env bash
set -euo pipefail
validation_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
docker build -t dr-evt-slurm-validation "$validation_dir"
exec bash "$validation_dir/licenses/run_event.sh"
