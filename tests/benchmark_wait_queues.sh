#!/bin/bash
# Wait Queue Performance Testing: Compare C++ queue implementations and the
# Python reference scheduler.
#
# Tests deque, multimap, circular, and block queue implementations with sizes
# 4, 8, 16, 32, 64, 128, and 256. It also times the Python reference using
# the same input trace and node count.
# - Verifies every C++ queue implementation matches deque output
# - Measures whole-simulation wall-clock time, including parsing, scheduling,
#   event handling, and trace output; it is not a wait-queue microbenchmark
# - Compares the Python and deque schedules after normalizing their schemas
#
# The default run deliberately benchmarks EASY and bounded hybrid backfill, not
# full conservative backfill. Conservative protects every waiting job and can
# take minutes on the default 10,000-job trace; run it only as an explicit,
# separate benchmark with a smaller trace (for example, huge_2000jobs.csv).

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."
source "$SCRIPT_DIR/test_reporting.sh"
test_report_enable

cd "$REPO_ROOT"

# Configuration
BLOCK_SIZES=(4 8 16 32 64 128 256)
TRACE_FILE=""
BENCHMARK_MODE="all"
# A depth of 1 reproduces EASY; a depth covering the examined queue approaches
# conservative backfill. Keep it no larger than the queue-depth cap: the
# built-in scheduler cannot protect jobs it does not examine in that pass.
HYBRID_RESERVATION_DEPTH=32
HYBRID_QUEUE_DEPTH=32
HYBRID_BACKFILL_INTERVAL=0
WITH_PYTHON_REFERENCE=0
DISPATCH_MODE=0
DISPATCH_INTERVAL=0
DISPATCH_QUEUE_DEPTH=0
DISPATCH_ON_EVENTS=0
DISPATCH_RUNS=3
DISPATCH_OPTIONS_SET=0

print_usage() {
    echo "Usage: $0 [OPTIONS] [TRACE_FILE]"
    echo ""
    echo "Whole-simulation benchmark: queue variants are compared through complete simulator runs, not isolated queue operations."
    echo "Variants: deque, multimap, circular, all block sizes, and Python reference"
    echo ""
    echo "ARGUMENTS:"
    echo "  --backfill-policy POLICY"
    echo "                all (default) runs EASY and hybrid comparisons;"
    echo "                easy benchmarks all FCFS queue variants; hybrid benchmarks"
    echo "                deque, circular, and the Python oracle. Conservative"
    echo "                benchmarks circular and the Python oracle when periodic."
    echo "  --reservation-depth N"
    echo "                Hybrid reservations to protect (default: 32)."
    echo "  --backfill-queue-depth N"
    echo "                Eligible jobs considered per hybrid pass (default: 32)."
    echo "  --backfill-interval SECONDS"
    echo "                Periodic backfill cadence; 0 keeps event-driven backfill."
    echo "  --dispatch-interval SECONDS"
    echo "                Unified periodic dispatch; choose easy, hybrid, conservative, or none."
    echo "  --dispatch-on-events"
    echo "                Also schedule arrivals between ticks (requires dispatch interval)."
    echo "  --dispatch-queue-depth N"
    echo "                Unified candidates per pass; 0 is unlimited (default: 0)."
    echo "  --runs N     Measured unified-dispatch runs after one warmup (default: 3)."
    echo "  --with-python-reference"
    echo "                Include the slow Python oracle in a periodic run."
    echo "  TRACE_FILE    Default: huge_10000jobs.csv; unified dispatch uses huge_2000jobs.csv."
    echo ""
    echo "EXAMPLES:"
    echo "  $0 --backfill-policy conservative --dispatch-interval 1 --dispatch-on-events --dispatch-queue-depth 10000"
    echo "  $0                                           # EASY and hybrid, default trace"
    echo "  $0 --backfill-policy easy                    # Reproduce the EASY table only"
    echo "  $0 --backfill-policy hybrid                  # Hybrid comparison (depth 32)"
    echo "  $0 --backfill-policy hybrid --backfill-interval 30"
    echo "  $0 --backfill-policy hybrid tests/test_traces/scale/huge_2000jobs.csv"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dispatch-on-events)
            DISPATCH_ON_EVENTS=1
            DISPATCH_OPTIONS_SET=1
            shift
            ;;
        --dispatch-interval)
            if [[ $# -lt 2 || ! "$2" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
                echo "Error: --dispatch-interval requires a positive number"
                exit 1
            fi
            DISPATCH_INTERVAL="$2"
            DISPATCH_MODE=1
            shift 2
            ;;
        --dispatch-queue-depth)
            if [[ $# -lt 2 || ! "$2" =~ ^[0-9]+$ ]]; then
                echo "Error: --dispatch-queue-depth requires a nonnegative integer"
                exit 1
            fi
            DISPATCH_QUEUE_DEPTH="$2"
            DISPATCH_OPTIONS_SET=1
            shift 2
            ;;
        --runs)
            if [[ $# -lt 2 || ! "$2" =~ ^[1-9][0-9]*$ ]]; then
                echo "Error: --runs requires a positive integer"
                exit 1
            fi
            DISPATCH_RUNS="$2"
            DISPATCH_OPTIONS_SET=1
            shift 2
            ;;
        --backfill-policy)
            if [[ $# -lt 2 ]]; then
                echo "Error: --backfill-policy requires all, easy, hybrid, or conservative"
                exit 1
            fi
            BENCHMARK_MODE="$2"
            shift 2
            ;;
        --reservation-depth)
            if [[ $# -lt 2 || ! "$2" =~ ^[1-9][0-9]*$ ]]; then
                echo "Error: --reservation-depth requires a positive integer"
                exit 1
            fi
            HYBRID_RESERVATION_DEPTH="$2"
            shift 2
            ;;
        --backfill-queue-depth)
            if [[ $# -lt 2 || ! "$2" =~ ^[1-9][0-9]*$ ]]; then
                echo "Error: --backfill-queue-depth requires a positive integer"
                exit 1
            fi
            HYBRID_QUEUE_DEPTH="$2"
            shift 2
            ;;
        --backfill-interval)
            if [[ $# -lt 2 || ! "$2" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
                echo "Error: --backfill-interval requires a nonnegative number"
                exit 1
            fi
            HYBRID_BACKFILL_INTERVAL="$2"
            shift 2
            ;;
        --with-python-reference)
            WITH_PYTHON_REFERENCE=1
            shift
            ;;
        -h|--help)
            print_usage
            ;;
        *)
            if [[ -n "$TRACE_FILE" ]]; then
                echo "Error: only one trace file may be specified"
                exit 1
            fi
            TRACE_FILE="$1"
            shift
            ;;
    esac
done

if [[ -z "$TRACE_FILE" ]]; then
    if [[ "$DISPATCH_MODE" == "1" ]]; then
        TRACE_FILE="tests/test_traces/scale/huge_2000jobs.csv"
    else
        TRACE_FILE="tests/test_traces/scale/huge_10000jobs.csv"
    fi
fi

if [[ "$BENCHMARK_MODE" != "all" && "$BENCHMARK_MODE" != "easy" && "$BENCHMARK_MODE" != "hybrid" && "$BENCHMARK_MODE" != "conservative" && !( "$BENCHMARK_MODE" == "none" && "$DISPATCH_MODE" == "1" ) ]]; then
    echo "Error: unsupported backfill policy '$BENCHMARK_MODE' (use all, easy, hybrid, or conservative)"
    exit 1
fi

if [[ "$DISPATCH_OPTIONS_SET" == "1" && "$DISPATCH_MODE" == "0" ]]; then
    echo "Error: --dispatch-on-events, --dispatch-queue-depth, and --runs require --dispatch-interval" >&2
    exit 1
fi
if [[ "$DISPATCH_MODE" == "1" && ( "$BENCHMARK_MODE" == "all" || "$HYBRID_BACKFILL_INTERVAL" != "0" || "$WITH_PYTHON_REFERENCE" != "0" ) ]]; then
    echo "Error: unified dispatch requires one --backfill-policy (easy, hybrid, conservative, or none), no --backfill-interval, and no Python oracle" >&2
    exit 1
fi

# Locate the installed simulator. This is shared with the other test scripts.
source "$SCRIPT_DIR/set_simulator_path.sh"
source "$SCRIPT_DIR/select_python.sh"

# Check trace file exists
if [[ ! -f "$TRACE_FILE" ]]; then
    echo "Error: Trace file not found: $TRACE_FILE"
    exit 1
fi

# Common simulation parameters for all benchmark modes.
TOTAL_NODES=500
MAX_JOBS=50000
TRACE_FORMAT="simple"
TIMESTAMP_FORMAT="epoch"
# Use run_time_mode=limit (jobs run for full time_limit) for consistent benchmarking.
RUN_TIME_MODE="limit"

# Unified dispatch has only a circular implementation. Measure complete runs
# rather than compare queue variants or invoke the separate-backfill oracle.
run_unified_dispatch_benchmark() {
    if ! select_python_interpreter 3 7; then
        echo "Error: Python 3.7+ is required for portable timing and reporting" >&2
        return 1
    fi
    "$PYTHON_BIN" - "$SIMULATOR" "$TRACE_FILE" "$BENCHMARK_MODE" \
        "$DISPATCH_INTERVAL" "$DISPATCH_QUEUE_DEPTH" "$DISPATCH_ON_EVENTS" \
        "$DISPATCH_RUNS" "$HYBRID_RESERVATION_DEPTH" "$TOTAL_NODES" "$MAX_JOBS" <<'PY'
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time

simulator, trace, policy, interval, depth, on_events, runs, reservations, nodes, max_jobs = sys.argv[1:]
if not math.isfinite(float(interval)) or float(interval) <= 0:
    sys.exit("Error: --dispatch-interval must be finite and positive")
outdir = Path(tempfile.mkdtemp(prefix="dr-evt-unified-benchmark-"))
common = [simulator, str(Path(trace).resolve()),
          "--priority_policy", "fcfs", "--queue_impl", "circular",
          "--backfill_policy", policy,
          "--dispatch_interval", interval, "--dispatch_queue_depth", depth,
          "--total_nodes", nodes, "--max_jobs", max_jobs,
          "--trace_format", "simple", "--timestamp_format", "epoch",
          "--run_time_mode", "limit"]
if on_events == "1":
    common.append("--dispatch_on_events")
if policy == "hybrid":
    common.extend(["--reservation_depth", reservations])
print("Unified dispatch: {}; interval: {} s; queue depth: {}; arrival passes: {}".format(
    policy, interval, depth, on_events == "1"))
if policy == "hybrid":
    print("Reservation depth: {}".format(reservations))
print("Trace: {}; nodes: {}; planning horizon: unlimited".format(trace, nodes))
print("Timing includes parsing, scheduling, event handling, and trace output.")
print("Artifacts: {}".format(outdir), flush=True)
elapsed_runs = []
reference_hashes = None
for run in range(int(runs) + 1):
    label = "warmup" if run == 0 else "run-{}".format(run)
    output = outdir / (label + ".csv")
    resources = outdir / (label + "-resources.csv")
    log = outdir / (label + ".log")
    command = common + ["--outfile", str(output), "--resource_trace", str(resources)]
    with log.open("wb") as stream:
        start = time.perf_counter()
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
        elapsed = time.perf_counter() - start
    if result.returncode:
        sys.exit("Simulator failed ({}); see {}".format(result.returncode, log))
    hashes = [hashlib.sha256(path.read_bytes()).hexdigest()
              for path in (output, resources)]
    if reference_hashes is None:
        reference_hashes = hashes
    elif hashes != reference_hashes:
        sys.exit("Repeated schedules differ; artifacts: {}".format(outdir))
    print("{}: {:.6f} s".format(label, elapsed), flush=True)
    if run:
        elapsed_runs.append(elapsed)
report = {"command": common, "seconds": elapsed_runs,
          "median_seconds": statistics.median(elapsed_runs),
          "warmup_runs": 1, "identical_outputs": True,
          "schedule_sha256": reference_hashes[0],
          "resource_trace_sha256": reference_hashes[1]}
(outdir / "results.json").write_text(json.dumps(report, indent=2) + "\n")
print("Median: {:.6f} s; repeated outputs identical".format(report["median_seconds"]))
PY
}

if [[ "$DISPATCH_MODE" == "1" ]]; then
    run_unified_dispatch_benchmark
    exit $?
fi

elapsed_seconds() {
    echo "scale=3; ($2 - $1) / 1000000000" | bc
}

run_periodic_backfill_benchmark() {
    local outdir trace_name deque_out circular_out python_out_dir
    local deque_time circular_time python_time python_status circular_status
    local cpp_start cpp_end python_start python_end
    local benchmark_label

    case "$BENCHMARK_MODE" in
        hybrid) benchmark_label="Hybrid" ;;
        conservative) benchmark_label="Conservative" ;;
        *) benchmark_label="$BENCHMARK_MODE" ;;
    esac

    # A periodic benchmark measures the C++ implementation by default.  The
    # Python oracle is deliberately opt-in because repeated reference passes
    # over a long trace can take impractically long.
    if [[ "$HYBRID_BACKFILL_INTERVAL" == "0" || "$WITH_PYTHON_REFERENCE" == "1" ]]; then
        if ! select_python_interpreter 3 7; then
            echo "Error: Python 3.7+ is required for the requested reference run"
            return 1
        fi
    fi
    if ! outdir=$(mktemp -d "${TMPDIR:-/tmp}/dr-evt-hybrid-benchmark.XXXXXXXX" 2>/dev/null); then
        outdir=$(mktemp -d "/tmp/dr-evt-hybrid-benchmark.XXXXXXXX")
    fi
    trace_name=$(basename "$TRACE_FILE" .csv)
    deque_out="$outdir/deque.csv"
    circular_out="$outdir/circular.csv"
    python_out_dir="$outdir/python"
    mkdir -p "$python_out_dir"
    cpp_policy_args=()
    if [[ "$BENCHMARK_MODE" == "hybrid" ]]; then
        cpp_policy_args=(--reservation_depth "$HYBRID_RESERVATION_DEPTH")
    fi

    run_periodic_cpp() {
        local label="$1" queue_impl="$2" output="$3" resources="$4" log="$5"
        cpp_start=$(date +%s%N)
        if ! "$SIMULATOR" "$TRACE_FILE" \
            --priority_policy fcfs_conservative \
            --queue_impl "$queue_impl" \
            --backfill_policy "$BENCHMARK_MODE" \
            "${cpp_policy_args[@]}" \
            --backfill_queue_depth "$HYBRID_QUEUE_DEPTH" \
            --backfill_interval "$HYBRID_BACKFILL_INTERVAL" \
            --total_nodes "$TOTAL_NODES" \
            --max_jobs "$MAX_JOBS" \
            --trace_format "$TRACE_FORMAT" \
            --timestamp_format "$TIMESTAMP_FORMAT" \
            --run_time_mode "$RUN_TIME_MODE" \
            --outfile "$output" \
            --resource_trace "$resources" >"$log" 2>&1; then
            echo "C++ $label run failed; see $log" >&2
            return 1
        fi
        cpp_end=$(date +%s%N)
        elapsed_seconds "$cpp_start" "$cpp_end"
    }

    echo ""
    echo "=========================================="
    echo "$benchmark_label Backfill Queue Comparison"
    echo "=========================================="
    echo "Trace: $TRACE_FILE"
    echo "Total nodes: $TOTAL_NODES"
    echo "Max jobs: $MAX_JOBS"
    if [[ "$BENCHMARK_MODE" == "hybrid" ]]; then
        echo "Reservation depth: $HYBRID_RESERVATION_DEPTH"
    fi
    echo "Queue depth: $HYBRID_QUEUE_DEPTH"
    echo "Backfill interval: $HYBRID_BACKFILL_INTERVAL"
    echo "Measurement: complete simulation wall-clock time (not isolated queue operations)"
    echo ""

    if [[ "$HYBRID_BACKFILL_INTERVAL" == "0" ]]; then
        echo "Running C++ deque..."
        deque_time=$(run_periodic_cpp "deque" deque "$deque_out" "$outdir/deque_resources.csv" "$outdir/deque.log")
        echo "done (${deque_time}s)"
    fi
    echo "Running C++ circular..."
    circular_time=$(run_periodic_cpp "circular" circular "$circular_out" "$outdir/circular_resources.csv" "$outdir/circular.log")
    echo "done (${circular_time}s)"

    if [[ "$HYBRID_BACKFILL_INTERVAL" == "0" ]]; then
        echo -n "Checking C++ schedule equivalence... "
        if diff -q "$deque_out" "$circular_out" >/dev/null 2>&1; then
            circular_status="PASS"
            echo "PASS"
        else
            circular_status="FAIL"
            echo "FAIL (outputs retained in $outdir)"
        fi
    else
        deque_out="$circular_out"
        deque_time="$circular_time"
        circular_status="PASS"
    fi

    if [[ "$HYBRID_BACKFILL_INTERVAL" != "0" && "$WITH_PYTHON_REFERENCE" != "1" ]]; then
        python_time="N/A"
        python_status="SKIP"
        echo "Skipping Python reference for periodic backfill (use --with-python-reference on a small trace)."
    else
        echo -n "Running Python reference... "
        python_start=$(date +%s%N)
        python_policy_args=()
        if [[ "$BENCHMARK_MODE" == "hybrid" ]]; then
            python_policy_args=(--reservation-depth "$HYBRID_RESERVATION_DEPTH")
        fi
        if "$PYTHON_BIN" scripts/python_hybrid_scheduler.py "$TRACE_FILE" \
            --nodes "$TOTAL_NODES" --outdir "$python_out_dir" \
            "${python_policy_args[@]}" \
            --backfill-queue-depth "$HYBRID_QUEUE_DEPTH" \
            --backfill-interval "$HYBRID_BACKFILL_INTERVAL" \
            >"$outdir/python.log" 2>&1; then
            python_end=$(date +%s%N)
            python_time=$(elapsed_seconds "$python_start" "$python_end")
            if [[ -f "$python_out_dir/${trace_name}_hybrid_reference.csv" ]] && \
                "$PYTHON_BIN" tests/compare_scheduler_outputs.py \
                    "$python_out_dir/${trace_name}_hybrid_reference.csv" \
                    "$deque_out" --tolerance 0.001 >"$outdir/python_compare.log" 2>&1; then
                python_status="PASS"
                echo "done (${python_time}s)"
            else
                python_status="FAIL"
                echo "FAIL (see $outdir/python_compare.log)"
            fi
        else
            python_status="FAIL"
            python_time="N/A"
            echo "FAIL (see $outdir/python.log)"
        fi
    fi

    echo ""
    if [[ "$HYBRID_BACKFILL_INTERVAL" == "0" ]]; then
        echo "Implementation | Time (s) | vs deque | Correctness"
        echo "---------------|----------|----------|------------"
        printf "%-14s | %-8s | %-8s | %-11s\n" "deque" "$deque_time" "1.00x" "baseline"
        printf "%-14s | %-8s | %-8s | %-11s\n" \
            "circular" "$circular_time" "$(echo "scale=2; $circular_time / $deque_time" | bc)x" "$circular_status"
    else
        echo "Implementation | Time (s) | vs circular | Correctness"
        echo "---------------|----------|-------------|------------"
        printf "%-14s | %-8s | %-8s | %-11s\n" "circular" "$circular_time" "1.00x" "baseline"
    fi
    if [[ "$python_time" == "N/A" ]]; then
        printf "%-14s | %-8s | %-8s | %-11s\n" "Python ref." "$python_time" "N/A" "$python_status"
    else
        printf "%-14s | %-8s | %-8s | %-11s\n" \
            "Python ref." "$python_time" "$(echo "scale=2; $python_time / $deque_time" | bc)x" "$python_status"
    fi

    if [[ "$circular_status" != "PASS" || ( "$python_status" != "PASS" && "$python_status" != "SKIP" ) ]]; then
        echo "Backfill benchmark correctness comparison failed. Outputs: $outdir"
        return 1
    fi
    rm -rf -- "$outdir"
}

if [[ "$BENCHMARK_MODE" == "hybrid" || "$BENCHMARK_MODE" == "conservative" ]]; then
    run_periodic_backfill_benchmark
    exit $?
fi

echo "=========================================="
echo "Wait Queue Performance Comparison"
echo "=========================================="
echo "Trace: $TRACE_FILE"
echo "Block sizes: ${BLOCK_SIZES[@]}"
echo "Also testing: circular, multimap, and Python reference"
echo ""

# Detect trace parameters
TRACE_NAME=$(basename "$TRACE_FILE" .csv)
NUM_JOBS=$(wc -l < "$TRACE_FILE" | tr -d ' ')

# This branch benchmarks EASY. Hybrid dispatches above to the focused
# deque/circular/Python comparison because block and multimap do not implement
# the same hybrid reservation algorithm. Periodic backfill has only a circular
# C++ implementation, so its hybrid branch compares circular and Python.
BACKFILL_POLICY="easy"

echo "Parameters:"
echo "  Jobs in trace: $NUM_JOBS"
echo "  Total nodes: $TOTAL_NODES"
echo "  Max jobs: $MAX_JOBS"
echo "  Backfill: $BACKFILL_POLICY"
echo "  Measurement: end-to-end wall-clock time (not isolated queue operations)"
echo ""

# Run baseline (deque)
echo "=========================================="
echo "1. BASELINE: Deque (default FCFS)"
echo "=========================================="

BASELINE_OUT="/tmp/baseline_deque.csv"
BASELINE_RESOURCES="/tmp/baseline_resources.csv"

echo -n "Running... "
START_TIME=$(date +%s%N)

"$SIMULATOR" "$TRACE_FILE" \
    --priority_policy fcfs \
    --queue_impl deque \
    --total_nodes $TOTAL_NODES \
    --max_jobs $MAX_JOBS \
    --trace_format $TRACE_FORMAT \
    --timestamp_format $TIMESTAMP_FORMAT \
    --run_time_mode $RUN_TIME_MODE \
    --backfill_policy $BACKFILL_POLICY \
    --outfile "$BASELINE_OUT" \
    --resource_trace "$BASELINE_RESOURCES" \
    > /tmp/baseline_log.txt 2>&1

END_TIME=$(date +%s%N)
BASELINE_TIME=$(echo "scale=3; ($END_TIME - $START_TIME) / 1000000000" | bc)

# Extract statistics
BASELINE_AVG_QUEUE=$(grep "Average queue length:" /tmp/baseline_log.txt | awk '{print $4}' || echo "N/A")
BASELINE_PEAK_QUEUE=$(grep "Peak queue length:" /tmp/baseline_log.txt | awk '{print $4}' || echo "N/A")

echo "done"
echo "  Time: ${BASELINE_TIME}s"
echo "  Avg queue: $BASELINE_AVG_QUEUE"
echo "  Peak queue: $BASELINE_PEAK_QUEUE"
echo ""

# Run all block sizes
echo "=========================================="
echo "2. BLOCK QUEUE: All Block Sizes"
echo "=========================================="

# Use arrays instead of associative arrays for compatibility
BLOCK_TIMES_4=""
BLOCK_TIMES_8=""
BLOCK_TIMES_16=""
BLOCK_TIMES_32=""
BLOCK_TIMES_64=""
BLOCK_TIMES_128=""
BLOCK_TIMES_256=""

BLOCK_AVG_QUEUES_4=""
BLOCK_AVG_QUEUES_8=""
BLOCK_AVG_QUEUES_16=""
BLOCK_AVG_QUEUES_32=""
BLOCK_AVG_QUEUES_64=""
BLOCK_AVG_QUEUES_128=""
BLOCK_AVG_QUEUES_256=""

BLOCK_PEAK_QUEUES_4=""
BLOCK_PEAK_QUEUES_8=""
BLOCK_PEAK_QUEUES_16=""
BLOCK_PEAK_QUEUES_32=""
BLOCK_PEAK_QUEUES_64=""
BLOCK_PEAK_QUEUES_128=""
BLOCK_PEAK_QUEUES_256=""

CORRECTNESS_4=""
CORRECTNESS_8=""
CORRECTNESS_16=""
CORRECTNESS_32=""
CORRECTNESS_64=""
CORRECTNESS_128=""
CORRECTNESS_256=""

for BLOCK_SIZE in "${BLOCK_SIZES[@]}"; do
    echo "--- Block Size: $BLOCK_SIZE ---"

    BLOCK_OUT="/tmp/block${BLOCK_SIZE}_output.csv"
    BLOCK_RESOURCES="/tmp/block${BLOCK_SIZE}_resources.csv"

    echo -n "  Running... "
    START_TIME=$(date +%s%N)

    "$SIMULATOR" "$TRACE_FILE" \
        --priority_policy fcfs \
        --queue_impl block \
        --block_size $BLOCK_SIZE \
        --total_nodes $TOTAL_NODES \
        --max_jobs $MAX_JOBS \
        --trace_format $TRACE_FORMAT \
        --timestamp_format $TIMESTAMP_FORMAT \
        --run_time_mode $RUN_TIME_MODE \
        --backfill_policy $BACKFILL_POLICY \
        --outfile "$BLOCK_OUT" \
        --resource_trace "$BLOCK_RESOURCES" \
        > /tmp/block${BLOCK_SIZE}_log.txt 2>&1

    END_TIME=$(date +%s%N)
    BLOCK_TIME=$(echo "scale=3; ($END_TIME - $START_TIME) / 1000000000" | bc)

    # Store in appropriate variable
    case $BLOCK_SIZE in
        4)   BLOCK_TIMES_4=$BLOCK_TIME ;;
        8)   BLOCK_TIMES_8=$BLOCK_TIME ;;
        16)  BLOCK_TIMES_16=$BLOCK_TIME ;;
        32)  BLOCK_TIMES_32=$BLOCK_TIME ;;
        64)  BLOCK_TIMES_64=$BLOCK_TIME ;;
        128) BLOCK_TIMES_128=$BLOCK_TIME ;;
        256) BLOCK_TIMES_256=$BLOCK_TIME ;;
    esac

    echo "done (${BLOCK_TIME}s)"

    # Extract statistics
    AVG_QUEUE=$(grep "Average queue length:" /tmp/block${BLOCK_SIZE}_log.txt | awk '{print $4}' || echo "N/A")
    PEAK_QUEUE=$(grep "Peak queue length:" /tmp/block${BLOCK_SIZE}_log.txt | awk '{print $4}' || echo "N/A")

    case $BLOCK_SIZE in
        4)   BLOCK_AVG_QUEUES_4=$AVG_QUEUE; BLOCK_PEAK_QUEUES_4=$PEAK_QUEUE ;;
        8)   BLOCK_AVG_QUEUES_8=$AVG_QUEUE; BLOCK_PEAK_QUEUES_8=$PEAK_QUEUE ;;
        16)  BLOCK_AVG_QUEUES_16=$AVG_QUEUE; BLOCK_PEAK_QUEUES_16=$PEAK_QUEUE ;;
        32)  BLOCK_AVG_QUEUES_32=$AVG_QUEUE; BLOCK_PEAK_QUEUES_32=$PEAK_QUEUE ;;
        64)  BLOCK_AVG_QUEUES_64=$AVG_QUEUE; BLOCK_PEAK_QUEUES_64=$PEAK_QUEUE ;;
        128) BLOCK_AVG_QUEUES_128=$AVG_QUEUE; BLOCK_PEAK_QUEUES_128=$PEAK_QUEUE ;;
        256) BLOCK_AVG_QUEUES_256=$AVG_QUEUE; BLOCK_PEAK_QUEUES_256=$PEAK_QUEUE ;;
    esac

    echo "  Avg queue: $AVG_QUEUE"
    echo "  Peak queue: $PEAK_QUEUE"

    # Verify correctness (compare with baseline)
    echo -n "  Checking correctness... "
    if diff -q "$BASELINE_OUT" "$BLOCK_OUT" > /dev/null 2>&1; then
        echo "✓ PASS (identical to deque)"
        case $BLOCK_SIZE in
            4)   CORRECTNESS_4="PASS" ;;
            8)   CORRECTNESS_8="PASS" ;;
            16)  CORRECTNESS_16="PASS" ;;
            32)  CORRECTNESS_32="PASS" ;;
            64)  CORRECTNESS_64="PASS" ;;
            128) CORRECTNESS_128="PASS" ;;
            256) CORRECTNESS_256="PASS" ;;
        esac
    else
        echo "✗ FAIL (differs from deque)"
        case $BLOCK_SIZE in
            4)   CORRECTNESS_4="FAIL" ;;
            8)   CORRECTNESS_8="FAIL" ;;
            16)  CORRECTNESS_16="FAIL" ;;
            32)  CORRECTNESS_32="FAIL" ;;
            64)  CORRECTNESS_64="FAIL" ;;
            128) CORRECTNESS_128="FAIL" ;;
            256) CORRECTNESS_256="FAIL" ;;
        esac
        echo "    Baseline: $BASELINE_OUT"
        echo "    Block-$BLOCK_SIZE: $BLOCK_OUT"
    fi

    echo ""
done

# Run multimap queue.
echo "=========================================="
echo "3. MULTIMAP QUEUE: fcfs_alt"
echo "=========================================="

MULTIMAP_OUT="/tmp/multimap_output.csv"
MULTIMAP_RESOURCES="/tmp/multimap_resources.csv"

echo -n "Running... "
START_TIME=$(date +%s%N)

"$SIMULATOR" "$TRACE_FILE" \
    --priority_policy fcfs_alt \
    --total_nodes $TOTAL_NODES \
    --max_jobs $MAX_JOBS \
    --trace_format $TRACE_FORMAT \
    --timestamp_format $TIMESTAMP_FORMAT \
    --run_time_mode $RUN_TIME_MODE \
    --backfill_policy $BACKFILL_POLICY \
    --outfile "$MULTIMAP_OUT" \
    --resource_trace "$MULTIMAP_RESOURCES" \
    > /tmp/multimap_log.txt 2>&1

END_TIME=$(date +%s%N)
MULTIMAP_TIME=$(echo "scale=3; ($END_TIME - $START_TIME) / 1000000000" | bc)
MULTIMAP_AVG_QUEUE=$(grep "Average queue length:" /tmp/multimap_log.txt | awk '{print $4}' || echo "N/A")
MULTIMAP_PEAK_QUEUE=$(grep "Peak queue length:" /tmp/multimap_log.txt | awk '{print $4}' || echo "N/A")

echo "done (${MULTIMAP_TIME}s)"
echo "  Avg queue: $MULTIMAP_AVG_QUEUE"
echo "  Peak queue: $MULTIMAP_PEAK_QUEUE"

echo -n "  Checking correctness... "
if diff -q "$BASELINE_OUT" "$MULTIMAP_OUT" > /dev/null 2>&1; then
    echo "✓ PASS (identical to deque)"
    CORRECTNESS_MULTIMAP="PASS"
else
    echo "✗ FAIL (differs from deque)"
    CORRECTNESS_MULTIMAP="FAIL"
    echo "    Baseline: $BASELINE_OUT"
    echo "    Multimap: $MULTIMAP_OUT"
fi

echo ""

# Run circular queue
echo "=========================================="
echo "4. CIRCULAR QUEUE: boost::circular_buffer"
echo "=========================================="

CIRCULAR_OUT="/tmp/circular_output.csv"
CIRCULAR_RESOURCES="/tmp/circular_resources.csv"

echo -n "Running... "
START_TIME=$(date +%s%N)

"$SIMULATOR" "$TRACE_FILE" \
    --priority_policy fcfs \
    --queue_impl circular \
    --total_nodes $TOTAL_NODES \
    --max_jobs $MAX_JOBS \
    --trace_format $TRACE_FORMAT \
    --timestamp_format $TIMESTAMP_FORMAT \
    --run_time_mode $RUN_TIME_MODE \
    --backfill_policy $BACKFILL_POLICY \
    --outfile "$CIRCULAR_OUT" \
    --resource_trace "$CIRCULAR_RESOURCES" \
    > /tmp/circular_log.txt 2>&1

END_TIME=$(date +%s%N)
CIRCULAR_TIME=$(echo "scale=3; ($END_TIME - $START_TIME) / 1000000000" | bc)

CIRCULAR_AVG_QUEUE=$(grep "Average queue length:" /tmp/circular_log.txt | awk '{print $4}' || echo "N/A")
CIRCULAR_PEAK_QUEUE=$(grep "Peak queue length:" /tmp/circular_log.txt | awk '{print $4}' || echo "N/A")

echo "done (${CIRCULAR_TIME}s)"
echo "  Avg queue: $CIRCULAR_AVG_QUEUE"
echo "  Peak queue: $CIRCULAR_PEAK_QUEUE"

echo -n "  Checking correctness... "
if diff -q "$BASELINE_OUT" "$CIRCULAR_OUT" > /dev/null 2>&1; then
    echo "✓ PASS (identical to deque)"
    CORRECTNESS_CIRCULAR="PASS"
else
    echo "✗ FAIL (differs from deque)"
    CORRECTNESS_CIRCULAR="FAIL"
    echo "    Baseline: $BASELINE_OUT"
    echo "    Circular: $CIRCULAR_OUT"
fi

echo ""

# Run the Python reference with the same trace and node count, then compare its
# schedule with the deque baseline after normalizing the two CSV schemas.
PYTHON_TIME=""
PYTHON_STATUS="unavailable"
echo "=========================================="
echo "5. PYTHON REFERENCE: EASY backfilling"
echo "=========================================="
if ! select_python_interpreter 3 6; then
    PYTHON_STATUS="FAIL"
    echo "FAILED (no Python 3.6+ interpreter found)"
elif [[ ! -f "scripts/python_reference_scheduler.py" ]]; then
    PYTHON_STATUS="FAIL"
    echo "FAILED (scripts/python_reference_scheduler.py not found)"
else
    if ! PYTHON_OUT_DIR=$(mktemp -d \
        "${TMPDIR:-/tmp}/dr-evt-python-reference.XXXXXXXX" 2>/dev/null); then
        PYTHON_OUT_DIR=$(mktemp -d "/tmp/dr-evt-python-reference.XXXXXXXX")
    fi
    PYTHON_LOG="$PYTHON_OUT_DIR/python_reference.log"
    echo -n "Running... "
    START_TIME=$(date +%s%N)
    if "$PYTHON_BIN" scripts/python_reference_scheduler.py "$TRACE_FILE" \
        --nodes "$TOTAL_NODES" \
        --outdir "$PYTHON_OUT_DIR" \
        > "$PYTHON_LOG" 2>&1; then
        END_TIME=$(date +%s%N)
        PYTHON_TIME=$(echo "scale=3; ($END_TIME - $START_TIME) / 1000000000" | bc)
        PYTHON_OUT="$PYTHON_OUT_DIR/${TRACE_NAME}_reference.csv"
        if [[ ! -f "$PYTHON_OUT" ]]; then
            PYTHON_STATUS="FAIL"
            echo "FAILED (reference output not found: $PYTHON_OUT)"
        elif "$PYTHON_BIN" tests/compare_scheduler_outputs.py \
            "$PYTHON_OUT" "$BASELINE_OUT" --tolerance 0.001 \
            > "$PYTHON_OUT_DIR/comparison.log" 2>&1; then
            PYTHON_STATUS="PASS"
            echo "done (${PYTHON_TIME}s)"
            echo "  Correctness: PASS (schedule matches deque within 0.001)"
        else
            PYTHON_STATUS="FAIL"
            echo "done (${PYTHON_TIME}s)"
            echo "  Correctness: FAIL"
            sed 's/^/  /' "$PYTHON_OUT_DIR/comparison.log"
        fi
    else
        PYTHON_STATUS="FAIL"
        echo "FAILED"
        sed 's/^/  /' "$PYTHON_LOG"
        echo "  Full log: $PYTHON_LOG"
    fi
fi
echo ""

# Summary table
echo "=========================================="
echo "6. SUMMARY: Performance & Correctness"
echo "=========================================="
echo ""

printf "%-12s | %-10s | %-10s | %-10s | %-10s | %-12s\n" \
    "Impl" "Time (s)" "vs Deque" "Slowdown" "Peak Queue" "Correctness"
echo "-------------|------------|------------|------------|------------|-------------"

# Track the overall best (fastest) implementation across deque, every
# block size, and circular - not just the best among block sizes, since
# either deque or circular can legitimately be the fastest overall.
BEST_OVERALL_NAME="Deque"
BEST_OVERALL_TIME=$BASELINE_TIME

# Baseline
printf "%-12s | %-10s | %-10s | %-10s | %-10s | %-12s\n" \
    "Deque" "$BASELINE_TIME" "1.00x" "baseline" "$BASELINE_PEAK_QUEUE" "baseline"

# Multimap queue
MULTIMAP_VS_DEQUE=$(echo "scale=2; $MULTIMAP_TIME / $BASELINE_TIME" | bc)
MULTIMAP_SLOWDOWN=$(printf "%.0f" $(echo "scale=4; (($MULTIMAP_TIME / $BASELINE_TIME) - 1.0) * 100.0" | bc))
if [[ "$MULTIMAP_SLOWDOWN" =~ ^- ]]; then
    MULTIMAP_SLOWDOWN_STR="${MULTIMAP_SLOWDOWN}%"
else
    MULTIMAP_SLOWDOWN_STR="+${MULTIMAP_SLOWDOWN}%"
fi
if (( $(echo "$MULTIMAP_TIME < $BEST_OVERALL_TIME" | bc -l) )); then
    BEST_OVERALL_TIME=$MULTIMAP_TIME
    BEST_OVERALL_NAME="Multimap"
fi
printf "%-12s | %-10s | %-10s | %-10s | %-10s | %-12s\n" \
    "Multimap" "$MULTIMAP_TIME" "${MULTIMAP_VS_DEQUE}x" "$MULTIMAP_SLOWDOWN_STR" "$MULTIMAP_PEAK_QUEUE" "$CORRECTNESS_MULTIMAP"

# Circular queue
CIRCULAR_VS_DEQUE=$(echo "scale=2; $CIRCULAR_TIME / $BASELINE_TIME" | bc)
CIRCULAR_SLOWDOWN=$(printf "%.0f" $(echo "scale=4; (($CIRCULAR_TIME / $BASELINE_TIME) - 1.0) * 100.0" | bc))
if [[ "$CIRCULAR_SLOWDOWN" =~ ^- ]]; then
    CIRCULAR_SLOWDOWN_STR="${CIRCULAR_SLOWDOWN}%"
else
    CIRCULAR_SLOWDOWN_STR="+${CIRCULAR_SLOWDOWN}%"
fi
if (( $(echo "$CIRCULAR_TIME < $BEST_OVERALL_TIME" | bc -l) )); then
    BEST_OVERALL_TIME=$CIRCULAR_TIME
    BEST_OVERALL_NAME="Circular"
fi
printf "%-12s | %-10s | %-10s | %-10s | %-10s | %-12s\n" \
    "Circular" "$CIRCULAR_TIME" "${CIRCULAR_VS_DEQUE}x" "$CIRCULAR_SLOWDOWN_STR" "$CIRCULAR_PEAK_QUEUE" "$CORRECTNESS_CIRCULAR"

# The Python reference is excluded from BEST_OVERALL_NAME: it is not an
# interchangeable C++ wait-queue backend.
if [[ -n "$PYTHON_TIME" ]]; then
    PYTHON_VS_DEQUE=$(echo "scale=2; $PYTHON_TIME / $BASELINE_TIME" | bc)
    PYTHON_SLOWDOWN=$(printf "%.0f" $(echo "scale=4; (($PYTHON_TIME / $BASELINE_TIME) - 1.0) * 100.0" | bc))
    if [[ "$PYTHON_SLOWDOWN" =~ ^- ]]; then
        PYTHON_SLOWDOWN_STR="${PYTHON_SLOWDOWN}%"
    else
        PYTHON_SLOWDOWN_STR="+${PYTHON_SLOWDOWN}%"
    fi
    printf "%-12s | %-10s | %-10s | %-10s | %-10s | %-12s\n" \
        "Python ref." "$PYTHON_TIME" "${PYTHON_VS_DEQUE}x" "$PYTHON_SLOWDOWN_STR" "N/A" "$PYTHON_STATUS"
fi

# Find best block size, and fold that into the overall-best tracking
BEST_BLOCK_SIZE=""
BEST_BLOCK_TIME=999999.0

for BLOCK_SIZE in "${BLOCK_SIZES[@]}"; do
    # Get values from appropriate variables
    case $BLOCK_SIZE in
        4)   TIME=$BLOCK_TIMES_4; AVG=$BLOCK_AVG_QUEUES_4; PEAK=$BLOCK_PEAK_QUEUES_4; CORRECT=$CORRECTNESS_4 ;;
        8)   TIME=$BLOCK_TIMES_8; AVG=$BLOCK_AVG_QUEUES_8; PEAK=$BLOCK_PEAK_QUEUES_8; CORRECT=$CORRECTNESS_8 ;;
        16)  TIME=$BLOCK_TIMES_16; AVG=$BLOCK_AVG_QUEUES_16; PEAK=$BLOCK_PEAK_QUEUES_16; CORRECT=$CORRECTNESS_16 ;;
        32)  TIME=$BLOCK_TIMES_32; AVG=$BLOCK_AVG_QUEUES_32; PEAK=$BLOCK_PEAK_QUEUES_32; CORRECT=$CORRECTNESS_32 ;;
        64)  TIME=$BLOCK_TIMES_64; AVG=$BLOCK_AVG_QUEUES_64; PEAK=$BLOCK_PEAK_QUEUES_64; CORRECT=$CORRECTNESS_64 ;;
        128) TIME=$BLOCK_TIMES_128; AVG=$BLOCK_AVG_QUEUES_128; PEAK=$BLOCK_PEAK_QUEUES_128; CORRECT=$CORRECTNESS_128 ;;
        256) TIME=$BLOCK_TIMES_256; AVG=$BLOCK_AVG_QUEUES_256; PEAK=$BLOCK_PEAK_QUEUES_256; CORRECT=$CORRECTNESS_256 ;;
    esac

    # Calculate speedup (handle leading dot in time like .963)
    VS_DEQUE=$(echo "scale=2; $TIME / $BASELINE_TIME" | bc)
    # Calculate slowdown percentage (scale AFTER multiplication to avoid truncation)
    SLOWDOWN=$(printf "%.0f" $(echo "scale=4; (($TIME / $BASELINE_TIME) - 1.0) * 100.0" | bc))

    # Handle negative slowdown (shouldn't happen, but check)
    if [[ "$SLOWDOWN" =~ ^- ]]; then
        SLOWDOWN_STR="${SLOWDOWN}%"
    else
        SLOWDOWN_STR="+${SLOWDOWN}%"
    fi

    # Mark best among block sizes specifically (for the "optimal block
    # size" note below), and separately fold into the overall-best
    # tracking across every implementation tested.
    MARKER=""
    if (( $(echo "$TIME < $BEST_BLOCK_TIME" | bc -l) )); then
        BEST_BLOCK_TIME=$TIME
        BEST_BLOCK_SIZE=$BLOCK_SIZE
        MARKER="*"
    fi
    if (( $(echo "$TIME < $BEST_OVERALL_TIME" | bc -l) )); then
        BEST_OVERALL_TIME=$TIME
        BEST_OVERALL_NAME="Block-${BLOCK_SIZE}"
    fi

    printf "%-12s | %-10s | %-10s | %-10s | %-10s | %-12s\n" \
        "Block-${BLOCK_SIZE}${MARKER}" "$TIME" "${VS_DEQUE}x" "$SLOWDOWN_STR" "$PEAK" "$CORRECT"
done

echo ""
echo "=========================================="
echo "7. CONCLUSIONS"
echo "=========================================="
echo ""

# Check whether all comparable C++ queue implementations passed.
ALL_PASSED=true
for BLOCK_SIZE in "${BLOCK_SIZES[@]}"; do
    case $BLOCK_SIZE in
        4)   CORRECT=$CORRECTNESS_4 ;;
        8)   CORRECT=$CORRECTNESS_8 ;;
        16)  CORRECT=$CORRECTNESS_16 ;;
        32)  CORRECT=$CORRECTNESS_32 ;;
        64)  CORRECT=$CORRECTNESS_64 ;;
        128) CORRECT=$CORRECTNESS_128 ;;
        256) CORRECT=$CORRECTNESS_256 ;;
    esac

    if [[ "$CORRECT" != "PASS" ]]; then
        ALL_PASSED=false
        break
    fi
done
if [[ "$CORRECTNESS_CIRCULAR" != "PASS" ]]; then
    ALL_PASSED=false
fi
if [[ "$CORRECTNESS_MULTIMAP" != "PASS" ]]; then
    ALL_PASSED=false
fi
if [[ "$PYTHON_STATUS" != "PASS" ]]; then
    ALL_PASSED=false
fi

if $ALL_PASSED; then
    echo "✓ All queue implementations and the Python reference agree"
else
    echo "✗ A required run failed or produced different output (INVESTIGATION NEEDED)"
fi

echo ""
echo "Best block size: Block-$BEST_BLOCK_SIZE"
echo "  Time: ${BEST_BLOCK_TIME}s"
BEST_BLOCK_VS_DEQUE=$(echo "scale=2; $BEST_BLOCK_TIME / $BASELINE_TIME" | bc)
if (( $(echo "$BEST_BLOCK_TIME < $BASELINE_TIME" | bc -l) )); then
    BEST_BLOCK_ADVANTAGE=$(printf "%.0f" $(echo "scale=4; (1 - $BEST_BLOCK_TIME / $BASELINE_TIME) * 100" | bc))
    echo "  vs Deque: ${BEST_BLOCK_VS_DEQUE}x (${BEST_BLOCK_ADVANTAGE}% faster)"
else
    BEST_BLOCK_SLOWDOWN=$(printf "%.0f" $(echo "scale=4; ($BEST_BLOCK_TIME / $BASELINE_TIME - 1) * 100" | bc))
    echo "  vs Deque: ${BEST_BLOCK_VS_DEQUE}x (${BEST_BLOCK_SLOWDOWN}% slower)"
fi
echo ""

# BEST_OVERALL_NAME/BEST_OVERALL_TIME were tracked across deque, every
# block size, and circular above - not just among block sizes - so this
# reflects whichever implementation actually won, rather than assuming
# deque always does.
echo "Best overall: $BEST_OVERALL_NAME"
echo "  Time: ${BEST_OVERALL_TIME}s"
if [[ "$BEST_OVERALL_NAME" == "Deque" ]]; then
    DEQUE_ADVANTAGE=$(printf "%.0f" $(echo "scale=4; ($BEST_BLOCK_TIME / $BASELINE_TIME - 1) * 100" | bc))
    echo "  Deque is ${DEQUE_ADVANTAGE}% faster than the best block size"
    echo ""
    echo "RECOMMENDATION: Use --queue_impl deque for this workload (circular is still the configured default)"
else
    ADVANTAGE=$(printf "%.0f" $(echo "scale=4; (1 - $BEST_OVERALL_TIME / $BASELINE_TIME) * 100" | bc))
    echo "  ${ADVANTAGE}% faster than deque"
    echo ""
    if [[ "$BEST_OVERALL_NAME" == Block-* ]]; then
        WINNING_BLOCK_SIZE="${BEST_OVERALL_NAME#Block-}"
        echo "RECOMMENDATION: Use --queue_impl block --block_size $WINNING_BLOCK_SIZE for this workload"
    else
        echo "RECOMMENDATION: circular is already the configured default (no flag needed) - confirmed best for this workload"
    fi
fi
echo ""

# Cleanup option
echo "Temporary files in /tmp/:"
echo "  baseline_*, block*_*.{csv,txt}, circular_*.{csv,txt}, multimap_*.{csv,txt}"
if [[ -n "${PYTHON_OUT_DIR:-}" ]]; then
    echo "  $PYTHON_OUT_DIR/"
fi
echo ""
echo "To clean up: rm /tmp/baseline_* /tmp/block* /tmp/circular_* /tmp/multimap_*"
if [[ -n "${PYTHON_OUT_DIR:-}" ]]; then
    echo "             rm -rf $PYTHON_OUT_DIR"
fi

if ! $ALL_PASSED; then
    exit 1
fi

if [[ "$BENCHMARK_MODE" == "all" ]]; then
    echo ""
    echo "Starting hybrid backfill comparison..."
    BENCHMARK_MODE="hybrid"
    run_periodic_backfill_benchmark
fi
