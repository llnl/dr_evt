#!/usr/bin/env bash
#SBATCH --job-name=easypower-fiscal-halves
#SBATCH --nodes=35
#SBATCH --ntasks=35
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=4
#SBATCH --exclusive
#SBATCH --time=06:00:00
#SBATCH --output=slurm-%x-%j.out
#SBATCH --error=slurm-%x-%j.err

# One allocation starts one independent, non-MPI job step for every fiscal-half
# and candidate-job-window pair. The default dataset has seven half-year
# workloads times five job windows, so all 35 steps can run concurrently on
# 35 nodes. Each step reserves four CPU cores and runs three EASYPower
# time-window simulations concurrently; one designated step per season also
# runs that season's single shared EASY baseline.

set -euo pipefail

batch_script_name=submit_easypower_fiscal_halves.slurm
if [[ -n ${EASYPOWER_SCRIPT_DIR:-} ]]; then
  script_dir=$(cd -- "$EASYPOWER_SCRIPT_DIR" && pwd)
elif [[ -n ${SLURM_SUBMIT_DIR:-} &&
        -f $SLURM_SUBMIT_DIR/experimental/fugaku-power/scripts/$batch_script_name ]]; then
  script_dir=$(cd -- "$SLURM_SUBMIT_DIR/experimental/fugaku-power/scripts" && pwd)
else
  script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
fi
if [[ -n ${EASYPOWER_REPO_DIR:-} ]]; then
  repo_dir=$(cd -- "$EASYPOWER_REPO_DIR" && pwd)
else
  repo_dir=$(cd -- "$script_dir/../../.." && pwd)
fi
batch_script="$script_dir/$batch_script_name"
export EASYPOWER_SCRIPT_DIR=$script_dir
export EASYPOWER_REPO_DIR=$repo_dir

# Required site/workload settings. Export these before sbatch or edit them.
: "${TRACE_DIR:?Set TRACE_DIR to the directory containing monthly traces}"
: "${HISTORICAL_TRACE_DIR:?Set HISTORICAL_TRACE_DIR to replay traces with begin/end times}"
: "${TOTAL_NODES:?Set TOTAL_NODES to the physical maximum}"

# Optional experiment settings.
OUTPUT_ROOT=${OUTPUT_ROOT:-$repo_dir/easypower-fiscal-sweeps}
TRACE_SUFFIX=${TRACE_SUFFIX:-_scheduling_trace.csv}
CANDIDATE_JOB_WINDOWS=${CANDIDATE_JOB_WINDOWS:-16,32,64,128,256}
CANDIDATE_TIME_WINDOWS=${CANDIDATE_TIME_WINDOWS:-unlimited,1h,6h}
MAXIMUM_POWER=${MAXIMUM_POWER:-12000000}
INITIAL_TARGET=${INITIAL_TARGET:-$MAXIMUM_POWER}
ENDPOINT_WINDOWS=${ENDPOINT_WINDOWS:-1m,5m,15m}
ALLOW_PARTIAL_HALVES=${ALLOW_PARTIAL_HALVES:-0}
BUILD_DRIVER=${BUILD_DRIVER:-0}
FY_START=${FY_START:-}
FY_END=${FY_END:-}
TRACE_TIMEZONE=${TRACE_TIMEZONE:-JST}
OVERLAP_POLICY=${OVERLAP_POLICY:-filename-month}
CAPACITY_SCENARIO=${CAPACITY_SCENARIO:-queue-pause-only}
CAPACITY_GENERATOR=${CAPACITY_GENERATOR:-$repo_dir/scripts/detect_queue_pause/generate_capacity_schedule.sh}
SWEEP_RUNNER=${SWEEP_RUNNER:-$script_dir/run_easypower_sweep.py}

for shared_executable in "$batch_script" "$CAPACITY_GENERATOR" "$SWEEP_RUNNER"; do
  if [[ $shared_executable != /* || ! -x $shared_executable ]]; then
    echo "required shared executable is unavailable: $shared_executable" >&2
    echo "submit through $script_dir/submit_and_monitor_easypower_fiscal.sh" >&2
    exit 2
  fi
done
batch_script=$(realpath -- "$batch_script")
CAPACITY_GENERATOR=$(realpath -- "$CAPACITY_GENERATOR")
SWEEP_RUNNER=$(realpath -- "$SWEEP_RUNNER")
export CAPACITY_GENERATOR SWEEP_RUNNER

if [[ -n $FY_START && -n $FY_END ]] && ((FY_START > FY_END)); then
  echo "FY_START must not exceed FY_END" >&2
  exit 2
fi
if [[ $ALLOW_PARTIAL_HALVES != 0 && $ALLOW_PARTIAL_HALVES != 1 ]]; then
  echo "ALLOW_PARTIAL_HALVES must be 0 or 1" >&2
  exit 2
fi

trace_dir=$(cd -- "$TRACE_DIR" && pwd)
historical_trace_dir=$(cd -- "$HISTORICAL_TRACE_DIR" && pwd)
mkdir -p -- "$OUTPUT_ROOT"
output_root=$(cd -- "$OUTPUT_ROOT" && pwd)
run_id=${RUN_ID:-${SLURM_JOB_ID:-manual-$(date +%Y%m%d-%H%M%S)-$$}}
run_root="$output_root/run-$run_id"
list_dir="$run_root/infile-lists"
if [[ -z ${EASYPOWER_WORKER_TASK_ID:-} ]]; then
  if ! mkdir -- "$run_root"; then
    echo "refusing to overwrite existing run directory: $run_root" >&2
    echo "submit again without reusing RUN_ID, or choose another OUTPUT_ROOT" >&2
    exit 2
  fi
  mkdir -- "$list_dir"
elif [[ ! -d $list_dir ]]; then
  echo "worker cannot find coordinator-created run directory: $run_root" >&2
  exit 2
fi

case $CAPACITY_SCENARIO in
queue-pause-only | with-reduced) ;;
*)
  echo "CAPACITY_SCENARIO must be queue-pause-only or with-reduced" >&2
  exit 2
  ;;
esac

# Build against the Boost matching GCC/OpenMPI. Loading Python before CMake can
# make CMake select Anaconda's ABI-incompatible Boost libraries.
module unload python/3.13.2 2>/dev/null || true
module load gcc/13.3.1-magic openmpi/4.1.2 boost/1.86.0 cmake/3.30.5
export OMP_NUM_THREADS=1

EASYPOWER_BUILD_DIR=${EASYPOWER_BUILD_DIR:-$repo_dir/experimental/fugaku-power/build}
export EASYPOWER_BUILD_DIR
build_dir=$EASYPOWER_BUILD_DIR
EASYPOWER_DRIVER=${EASYPOWER_DRIVER:-$build_dir/easypower_experiment}
export EASYPOWER_DRIVER
driver=$EASYPOWER_DRIVER
if [[ $BUILD_DRIVER == 1 ]]; then
  "$script_dir/build_easypower_experiment.sh" \
    >"$run_root/build.log" 2>&1
fi
if [[ ! -x $driver ]]; then
  echo "EASYPower driver is not executable: $driver" >&2
  echo "build it first with scripts/build_easypower_experiment.sh" >&2
  echo "or submit with BUILD_DRIVER=1" >&2
  exit 2
fi
module load python/3.13.2

declare -a workload_labels=()
declare -A files_by_half=()
declare -A months_by_half=()

# Japanese fiscal years begin in April: H1 is April--September, while H2 is
# October--March. As a deliberate dataset-boundary exception, any leading
# January--March files are prepended to the upcoming fiscal year's H1; thus the
# first available 21_03 trace belongs to the FY2021-H1 experiment.
shopt -s nullglob
monthly_traces=(
  "$trace_dir"/[0-9][0-9]_[0-9][0-9]"$TRACE_SUFFIX"
)
if ((${#monthly_traces[@]} == 0)); then
  echo "no monthly traces matching YY_MM${TRACE_SUFFIX} in $trace_dir" >&2
  exit 2
fi
first_base_name=$(basename -- "${monthly_traces[0]}")
if [[ ! $first_base_name =~ ^([0-9]{2})_([0-9]{2}) ]]; then
  echo "cannot parse first monthly trace filename: $first_base_name" >&2
  exit 2
fi
first_calendar_year=$((2000 + 10#${BASH_REMATCH[1]}))
first_month=$((10#${BASH_REMATCH[2]}))
leading_extended_label=
if ((first_month <= 3)); then
  leading_extended_label="FY${first_calendar_year}-H1"
fi

for trace_path in "${monthly_traces[@]}"; do
  base_name=$(basename -- "$trace_path")
  if [[ ! $base_name =~ ^([0-9]{2})_([0-9]{2}) ]]; then
    continue
  fi
  calendar_year=$((2000 + 10#${BASH_REMATCH[1]}))
  month=$((10#${BASH_REMATCH[2]}))
  if ((month < 1 || month > 12)); then
    echo "invalid month in trace filename: $base_name" >&2
    exit 2
  elif ((calendar_year == first_calendar_year && first_month <= 3 &&
          month <= 3)); then
    fiscal_year=$calendar_year
    half=H1
  elif ((month <= 3)); then
    fiscal_year=$((calendar_year - 1))
    half=H2
  elif ((month <= 9)); then
    fiscal_year=$calendar_year
    half=H1
  else
    fiscal_year=$calendar_year
    half=H2
  fi
  if [[ -n $FY_START ]] && ((fiscal_year < FY_START)); then
    continue
  fi
  if [[ -n $FY_END ]] && ((fiscal_year > FY_END)); then
    continue
  fi

  label="FY${fiscal_year}-${half}"
  if [[ -z ${files_by_half[$label]+present} ]]; then
    workload_labels+=("$label")
    files_by_half[$label]=
    months_by_half[$label]=0
  fi
  files_by_half[$label]+="$trace_path"$'\n'
  months_by_half[$label]=$((months_by_half[$label] + 1))
done

if ((${#workload_labels[@]} == 0)); then
  echo "no monthly traces remain after the FY filters" >&2
  exit 2
fi

for index in "${!workload_labels[@]}"; do
  label=${workload_labels[index]}
  month_count=${months_by_half[$label]}
  expected_months=6
  if [[ -n $leading_extended_label && $label == "$leading_extended_label" ]]; then
    expected_months=$((6 + 4 - first_month))
  fi
  if ((month_count != expected_months)); then
    is_edge=0
    if ((index == 0 || index == ${#workload_labels[@]} - 1)); then
      is_edge=1
    fi
    if [[ $ALLOW_PARTIAL_HALVES == 0 && $is_edge == 0 ]]; then
      echo "$label has $month_count months rather than $expected_months" >&2
      echo "set ALLOW_PARTIAL_HALVES=1 to include partial interior periods" >&2
      exit 2
    fi
    echo "WARNING: including partial $label with $month_count month(s)" >&2
  fi
done

IFS=',' read -r -a candidate_job_windows <<<"$CANDIDATE_JOB_WINDOWS"
if ((${#candidate_job_windows[@]} == 0)); then
  echo "CANDIDATE_JOB_WINDOWS must contain at least one value" >&2
  exit 2
fi

half_count=${#workload_labels[@]}
combination_count=$((half_count * ${#candidate_job_windows[@]}))

# The batch-script process coordinates the allocation. Each srun command below
# creates one single-process job step on one exclusive node; --mpi=none avoids
# starting an MPI runtime. Workers re-enter this script with a unique task ID.
if [[ -z ${EASYPOWER_WORKER_TASK_ID:-} && -n ${SLURM_JOB_ID:-} ]]; then
  allocated_tasks=${SLURM_NTASKS:-0}
  if ((allocated_tasks < combination_count)); then
    echo "allocation has $allocated_tasks tasks but $combination_count are required" >&2
    echo "request at least --nodes=$combination_count --ntasks=$combination_count" >&2
    exit 2
  fi

  logs_dir="$run_root/task-logs"
  mkdir -- "$logs_dir"
  declare -a worker_pids=()
  for ((task_id = 0; task_id < combination_count; ++task_id)); do
    srun --exclusive --exact --nodes=1 --ntasks=1 --cpus-per-task=4 \
      --mpi=none env \
      EASYPOWER_WORKER_TASK_ID="$task_id" \
      RUN_ID="$run_id" OUTPUT_ROOT="$output_root" \
      EASYPOWER_SCRIPT_DIR="$script_dir" \
      EASYPOWER_REPO_DIR="$repo_dir" \
      BUILD_DRIVER=0 EASYPOWER_BUILD_DIR="$build_dir" \
      EASYPOWER_DRIVER="$driver" \
      CAPACITY_GENERATOR="$CAPACITY_GENERATOR" \
      SWEEP_RUNNER="$SWEEP_RUNNER" \
      "$batch_script" \
      >"$logs_dir/task-$task_id.log" 2>&1 &
    worker_pids+=("$!")
  done

  failed=0
  for worker_pid in "${worker_pids[@]}"; do
    if ! wait "$worker_pid"; then
      failed=1
    fi
  done
  if ((failed)); then
    echo "one or more fiscal-half sweep steps failed; inspect $logs_dir" >&2
    exit 1
  fi
  echo "all $combination_count fiscal-half sweep steps completed"
  exit 0
fi

task_id=${EASYPOWER_WORKER_TASK_ID:-0}
if ((task_id >= combination_count)); then
  echo "worker task $task_id has no fiscal-half/job-window combination"
  exit 0
fi

# Interleave halves so adjacent workers initially use different seasons.
half_index=$((task_id % half_count))
window_index=$((task_id / half_count))
label=${workload_labels[half_index]}
job_window=${candidate_job_windows[window_index]}
half_root="$run_root/$label"
task_output="$half_root/job-window-$job_window"
task_list_dir="$list_dir/job-window-$job_window"
list_path="$task_list_dir/${label}.txt"
mkdir -p -- "$half_root"
if [[ -e $task_output ]]; then
  echo "refusing to overwrite existing task output: $task_output" >&2
  echo "submit again without reusing RUN_ID, or choose another OUTPUT_ROOT" >&2
  exit 2
fi
mkdir -p -- "$task_list_dir"
printf '%s' "${files_by_half[$label]}" >"$list_path"

# The detector consumes historical replay records (with begin/end times),
# while the simulation list contains the corresponding no-times inputs. The
# capacity output is shared by all job-window steps for this half and guarded
# so only one step generates it during a submission.
detection_output="$half_root/capacity-detection"
generation_id=${SLURM_JOB_ID:-manual}
complete_marker="$detection_output/.complete-$generation_id"
mkdir -p -- "$detection_output"
exec {capacity_lock_fd}>"$half_root/.capacity-detection.lock"
flock "$capacity_lock_fd"
if [[ ! -f $complete_marker ]]; then
  detection_input="$detection_output/input-$generation_id"
  mkdir -p -- "$detection_input"
  while IFS= read -r simulation_trace; do
    historical_trace="$historical_trace_dir/$(basename -- "$simulation_trace")"
    if [[ ! -f $historical_trace ]]; then
      echo "$label is missing historical trace $historical_trace" >&2
      exit 1
    fi
    link_path="$detection_input/$(basename -- "$historical_trace")"
    if [[ ! -e $link_path ]]; then
      ln -s -- "$historical_trace" "$link_path"
    fi
  done <"$list_path"

  echo "detecting queue pauses for $label"
  CAPACITY_NODES="$TOTAL_NODES" TRACE_TIMEZONE="$TRACE_TIMEZONE" \
    OVERLAP_POLICY="$OVERLAP_POLICY" SIMULATOR_FORMAT=1 \
    "$CAPACITY_GENERATOR" \
    "$detection_input/*${TRACE_SUFFIX}" "$detection_output" \
    >"$half_root/capacity-detection-$generation_id.log" 2>&1
  touch "$complete_marker"
fi
flock -u "$capacity_lock_fd"

if [[ $CAPACITY_SCENARIO == with-reduced ]]; then
  capacity_schedule="$detection_output/resource_capacity_with_reduced_capacity.csv"
else
  capacity_schedule="$detection_output/resource_capacity_without_reduced_capacity.csv"
fi
if [[ ! -s $capacity_schedule ]]; then
  echo "$label did not produce capacity schedule $capacity_schedule" >&2
  exit 1
fi

echo "running $label with candidate-job window $job_window"
baseline_options=(--shared-baseline-dir "$half_root/easy-baseline")
if ((window_index == 0)); then
  baseline_options+=(--run-shared-baseline)
fi
"$SWEEP_RUNNER" \
  --infile-list "$list_path" \
  --total-nodes "$TOTAL_NODES" \
  --candidate-job-windows "$job_window" \
  --candidate-time-windows "$CANDIDATE_TIME_WINDOWS" \
  --maximum-power "$MAXIMUM_POWER" \
  --initial-target "$INITIAL_TARGET" \
  --endpoint-windows "$ENDPOINT_WINDOWS" \
  --parallel-runs 4 \
  "${baseline_options[@]}" \
  --capacity-schedule "$capacity_schedule" \
  --driver "$driver" \
  --output-dir "$task_output"
touch "$task_output/.complete"
