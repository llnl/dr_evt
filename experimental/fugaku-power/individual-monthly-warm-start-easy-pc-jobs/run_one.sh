#!/usr/bin/env bash

# Run one EASY+PC mean- or maximum-power monthly reference simulation.

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../../.." && pwd)
usage() { echo "Usage: run_one.sh {mean|max} YYYY-MM" >&2; }
(($# == 2)) || { usage; exit 2; }
power_mode=$1
month=$2
case $power_mode in mean|max) ;; *) usage; exit 2 ;; esac
[[ $month =~ ^20[0-9]{2}-(0[1-9]|1[0-2])$ ]] || { echo "invalid month: $month" >&2; exit 2; }

output_root=${OUTPUT_ROOT:-$repo_dir/experimental/fugaku-power/results/individual-monthly-warm-start-easy-pc-sweep}
shared_output_root=${SHARED_OUTPUT_ROOT:-$repo_dir/experimental/fugaku-power/results/individual-monthly-warm-start-sweep}
driver=${EASYPOWER_DRIVER:-$repo_dir/experimental/fugaku-power/build/easypower_experiment}
total_nodes=${TOTAL_NODES:-158976}
maximum_power=${MAXIMUM_POWER:-12000000}
capacity_scenario=${CAPACITY_SCENARIO:-queue-pause-only}
trace_timezone=${TRACE_TIMEZONE:-JST}
case $capacity_scenario in
  queue-pause-only) capacity_file=resource_capacity_without_reduced_capacity.csv ;;
  with-reduced) capacity_file=resource_capacity_with_reduced_capacity.csv ;;
  *) echo "invalid CAPACITY_SCENARIO" >&2; exit 2 ;;
esac
[[ $total_nodes =~ ^[0-9]+$ ]] && ((total_nodes > 0)) || { echo "TOTAL_NODES must be positive" >&2; exit 2; }

shared_month="$shared_output_root/$month"
capacity_dir="$shared_month/capacity-detection"
input_dir="$shared_month/input"
capacity_schedule="$capacity_dir/$capacity_file"
baseline="$shared_month/easy-baseline"
if [[ ! -f $capacity_dir/.complete || ! -s $capacity_schedule ||
      ! -f $baseline/.complete || ! -s $baseline/jobs.csv ||
      ! -s $baseline/resources.csv ||
      ! -s $input_dir/warm-start-scheduling_trace.csv ||
      ! -s $input_dir/infile.txt ]]; then
  echo "shared input, capacity, or baseline is incomplete for $month under $shared_output_root" >&2
  exit 2
fi
[[ -x $driver ]] || { echo "EASYPower experiment driver unavailable: $driver" >&2; exit 2; }

month_root="$output_root/$month"
final="$month_root/easy-pc-$power_mode"
if [[ -f $final/.complete && -s $final/jobs.csv && -s $final/resources.csv ]]; then
  echo "EASY+PC-$power_mode already complete"
  exit 0
fi
[[ ! -e $final ]] || { echo "refusing to overwrite incomplete result: $final" >&2; exit 2; }
mkdir -p -- "$month_root"
staging="$final.tmp-${SLURM_JOB_ID:-manual-$$}"
[[ ! -e $staging ]] || { echo "staging directory already exists: $staging" >&2; exit 2; }
mkdir -- "$staging"

module load python/3.13.2
cutoff=$(python3 -c '
from datetime import datetime
from zoneinfo import ZoneInfo
import sys
label, timezone, override = sys.argv[1:]
timezone = "Asia/Tokyo" if timezone == "JST" else timezone
if override:
    try:
        print("{:.15g}".format(float(override)))
    except ValueError:
        parsed = datetime.fromisoformat(override.replace("Z", "+00:00"))
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=ZoneInfo(timezone))
        print("{:.15g}".format(parsed.timestamp()))
else:
    year, value = map(int, label.split("-"))
    year, value = (year + 1, 1) if value == 12 else (year, value + 1)
    print("{:.15g}".format(datetime(year, value, 1, tzinfo=ZoneInfo(timezone)).timestamp()))
' "$month" "$trace_timezone" "${SIM_END_TIME:-}")
module unload python/3.13.2 2>/dev/null || true
module load gcc/13.3.1-magic openmpi/4.1.2 boost/1.86.0 cmake/3.30.5
export OMP_NUM_THREADS=1

"$driver" "easy-pc-$power_mode-progressive" "$input_dir/infile.txt" \
  "$staging" "$total_nodes" 1 "$maximum_power" "$maximum_power" 0 \
  "$capacity_schedule" "$cutoff" \
  >"$month_root/easy-pc-$power_mode-${SLURM_JOB_ID:-manual-$$}.log" 2>&1
[[ -s $staging/jobs.csv && -s $staging/resources.csv ]] || {
  echo "EASY+PC-$power_mode output incomplete" >&2
  exit 1
}
touch "$staging/.complete"
mv -- "$staging" "$final"
