#!/bin/bash
# Wait Queue Performance Testing: Compare C++ queue implementations and the
# Python reference scheduler.
#
# Tests deque, multimap, circular, and block queue implementations with sizes
# 4, 8, 16, 32, 64, 128, and 256. It also times the Python reference using
# the same input trace and node count.
# - Verifies every C++ queue implementation matches deque output
# - Measures end-to-end wall-clock time, including parsing and trace output
# - Does not byte-compare Python output because its CSV schema differs

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

# Configuration
BLOCK_SIZES=(4 8 16 32 64 128 256)
TRACE_FILE="${1:-tests/test_traces/scale/huge_10000jobs.csv}"
VERBOSE=false

print_usage() {
    echo "Usage: $0 [TRACE_FILE]"
    echo ""
    echo "End-to-end queue benchmark: deque, multimap, circular, all block sizes, and Python reference"
    echo ""
    echo "ARGUMENTS:"
    echo "  TRACE_FILE    Path to test trace (default: tests/test_traces/scale/huge_10000jobs.csv)"
    echo ""
    echo "EXAMPLES:"
    echo "  $0                                           # Use default 10K job trace"
    echo "  $0 test_traces/scale/huge_20000jobs.csv     # Use 20K job trace"
    exit 0
}

if [[ "$1" == "-h" ]] || [[ "$1" == "--help" ]]; then
    print_usage
fi

# Locate the installed simulator when CMAKE_INSTALL_PREFIX is set, otherwise
# use the build-tree binary. This is shared with the other test scripts.
source "$SCRIPT_DIR/set_simulator_path.sh"

# Check trace file exists
if [[ ! -f "$TRACE_FILE" ]]; then
    echo "Error: Trace file not found: $TRACE_FILE"
    exit 1
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

# Common simulation parameters
TOTAL_NODES=500
MAX_JOBS=50000
TRACE_FORMAT="simple"
TIMESTAMP_FORMAT="epoch"
# Use run_time_mode=limit (jobs run for full time_limit) for consistent benchmarking
RUN_TIME_MODE="limit"
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

# Run the Python reference with the same trace and node count. It deliberately
# writes its own CSV schema, so this is an end-to-end performance reference,
# not a byte-for-byte correctness comparison with the C++ output.
PYTHON_TIME=""
PYTHON_STATUS="unavailable"
if command -v python3 > /dev/null 2>&1 && [[ -f "scripts/python_reference_scheduler.py" ]]; then
    echo "=========================================="
    echo "5. PYTHON REFERENCE: EASY backfilling"
    echo "=========================================="

    PYTHON_OUT_DIR="/tmp/python_reference_benchmark"
    mkdir -p "$PYTHON_OUT_DIR"
    echo -n "Running... "
    START_TIME=$(date +%s%N)
    if python3 scripts/python_reference_scheduler.py "$TRACE_FILE" \
        --nodes "$TOTAL_NODES" \
        --outdir "$PYTHON_OUT_DIR" \
        > /tmp/python_reference_log.txt 2>&1; then
        END_TIME=$(date +%s%N)
        PYTHON_TIME=$(echo "scale=3; ($END_TIME - $START_TIME) / 1000000000" | bc)
        PYTHON_STATUS="reference only"
        echo "done (${PYTHON_TIME}s)"
    else
        PYTHON_STATUS="FAILED"
        echo "FAILED (see /tmp/python_reference_log.txt)"
    fi
    echo "  Correctness: not byte-compared (different CSV schema)"
    echo ""
fi

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

if $ALL_PASSED; then
    echo "✓ All C++ queue implementations produce identical output (correctness verified)"
else
    echo "✗ Some implementation produced different output (INVESTIGATION NEEDED)"
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
echo "  baseline_*, block*_*.{csv,txt}, circular_*.{csv,txt}, multimap_*.{csv,txt}, python_reference_benchmark/"
echo ""
echo "To clean up: rm /tmp/baseline_* /tmp/block* /tmp/circular_* /tmp/multimap_*"
echo "             rm -rf /tmp/python_reference_benchmark"
