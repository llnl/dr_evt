#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
for input_list in "$script_dir"/infile-lists/FY*-H?.txt; do
  label=$(basename -- "$input_list" .txt)
  for job_window in 16 32 64 128 256; do
    "$script_dir/analyze_one.sh" "$label" "$job_window"
  done
done
