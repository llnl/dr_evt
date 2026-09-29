#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export YEARS=2022
exec "$script_dir/run_n_left.sh" "${1:-30}"
