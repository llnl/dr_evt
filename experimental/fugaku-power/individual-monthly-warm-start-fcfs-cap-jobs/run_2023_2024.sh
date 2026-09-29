#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export YEARS="2023 2024"
exec "$script_dir/run_n_left.sh" "${1:-12}"
