# Shared implementation for the monthly warm-start power-cap submitters.
# The suite wrapper defines n, manifest_kind, output_name, and suite_dir before
# sourcing this file. It may also define max_n and years.

common_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$common_dir/../../.." && pwd)

if [[ ! $n =~ ^[1-9][0-9]*$ ]]; then
  echo "usage: run_n_left.sh [POSITIVE_COUNT]" >&2
  exit 2
fi
if [[ -n ${max_n:-} ]] && ((n > max_n)); then
  echo "usage: run_n_left.sh [COUNT_FROM_1_TO_$max_n]" >&2
  exit 2
fi

years=${years:-}
for year in $years; do
  [[ $year =~ ^[0-9]{4}$ ]] || {
    echo "YEARS must contain space-separated four-digit years" >&2
    exit 2
  }
done

output_root=${OUTPUT_ROOT:-$repo_dir/experimental/fugaku-power/results/$output_name}
shared_output_root=${SHARED_OUTPUT_ROOT:-$repo_dir/experimental/fugaku-power/results/individual-monthly-warm-start-sweep}
manifest=${MANIFEST_PATH:-$suite_dir/manifest.tsv}
capacity_scenario=${CAPACITY_SCENARIO:-queue-pause-only}
case $capacity_scenario in
  queue-pause-only) capacity_file=resource_capacity_without_reduced_capacity.csv ;;
  with-reduced) capacity_file=resource_capacity_with_reduced_capacity.csv ;;
  *) echo "invalid CAPACITY_SCENARIO" >&2; exit 2 ;;
esac

submitted=0
while IFS=$'\t' read -r kind month window time_window script prerequisite; do
  [[ $kind == "$manifest_kind" ]] || continue
  if [[ -n $years ]]; then
    [[ " $years " == *" ${month%%-*} "* ]] || continue
  fi
  case $time_window in
    unlimited) slug=unlimited ;;
    1h) slug=3600s ;;
    6h) slug=21600s ;;
    *) echo "unknown time window in manifest: $time_window" >&2; exit 2 ;;
  esac

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

  result="$output_root/$month/job-window-$window/$month/easypower_n${window}_t${slug}"
  if [[ ! -f $result/.complete || ! -s $result/jobs.csv ||
        ! -s $result/resources.csv || ! -s $result/target_horizon.csv ]]; then
    sbatch "$suite_dir/$script"
    ((submitted += 1))
  fi
  ((submitted < n)) || break
done < "$manifest"

echo "Submitted $submitted jobs"
