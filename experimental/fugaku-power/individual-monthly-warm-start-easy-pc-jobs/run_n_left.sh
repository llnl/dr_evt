#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../../.." && pwd)
limit=${1:-12}
[[ $limit =~ ^[1-9][0-9]*$ ]] || { echo "usage: run_n_left.sh [POSITIVE_COUNT]" >&2; exit 2; }
output_root=${OUTPUT_ROOT:-$repo_dir/experimental/fugaku-power/results/individual-monthly-warm-start-easy-pc-sweep}
shared_output_root=${SHARED_OUTPUT_ROOT:-$repo_dir/experimental/fugaku-power/results/individual-monthly-warm-start-sweep}
manifest=${MANIFEST:-$script_dir/manifest.tsv}
capacity_scenario=${CAPACITY_SCENARIO:-queue-pause-only}
case $capacity_scenario in
  queue-pause-only) capacity_file=resource_capacity_without_reduced_capacity.csv ;;
  with-reduced) capacity_file=resource_capacity_with_reduced_capacity.csv ;;
  *) echo "invalid CAPACITY_SCENARIO" >&2; exit 2 ;;
esac

submitted=0
while IFS=$'\t' read -r kind month power_mode script prerequisite; do
  [[ $kind == easy-pc ]] || continue
  shared_month="$shared_output_root/$month"
  if [[ ! -f $shared_month/capacity-detection/.complete ||
        ! -s $shared_month/capacity-detection/$capacity_file ||
        ! -f $shared_month/easy-baseline/.complete ||
        ! -s $shared_month/easy-baseline/jobs.csv ||
        ! -s $shared_month/easy-baseline/resources.csv ||
        ! -s $shared_month/input/warm-start-scheduling_trace.csv ||
        ! -s $shared_month/input/infile.txt ]]; then
    echo "shared input, capacity, or baseline is incomplete for $month under $shared_output_root" >&2
    exit 2
  fi
  result="$output_root/$month/easy-pc-$power_mode"
  if [[ ! -f $result/.complete || ! -s $result/jobs.csv ||
        ! -s $result/resources.csv ]]; then
    sbatch "$script_dir/$script"
    ((submitted += 1))
  fi
  ((submitted < limit)) || break
done < "$manifest"
echo "Submitted $submitted jobs"
