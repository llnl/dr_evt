#!/bin/bash
# Block Queue Performance Testing: Compare all block sizes
#
# Tests block queue implementations with sizes: 16, 32, 64, 128, 256
# - Verifies correctness (all must match deque output)
# - Measures performance (time and queue statistics)
# - Identifies optimal block size

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

# Configuration
BLOCK_SIZES=(4 8 16 32 64 128 256)
TRACE_FILE="${1:-test_traces/scale/large_10000jobs.csv}"
VERBOSE=false

print_usage() {
    echo "Usage: $0 [TRACE_FILE]"
    echo ""
    echo "Block queue performance testing across all block sizes"
    echo ""
    echo "ARGUMENTS:"
    echo "  TRACE_FILE    Path to test trace (default: test_traces/scale/large_10000jobs.csv)"
    echo ""
    echo "EXAMPLES:"
    echo "  $0                                           # Use default 10K job trace"
    echo "  $0 test_traces/scale/huge_20000jobs.csv     # Use 20K job trace"
    exit 0
}

if [[ "$1" == "-h" ]] || [[ "$1" == "--help" ]]; then
    print_usage
fi

# Check trace file exists
if [[ ! -f "$TRACE_FILE" ]]; then
    echo "Error: Trace file not found: $TRACE_FILE"
    exit 1
fi

# Check build exists
if [[ ! -f "build/simulator" ]]; then
    echo "Error: Simulator not built. Run 'cmake --build build' first."
    exit 1
fi

echo "=========================================="
echo "Block Queue Performance Comparison"
echo "=========================================="
echo "Trace: $TRACE_FILE"
echo "Block sizes: ${BLOCK_SIZES[@]}"
echo ""

# Detect trace parameters
TRACE_NAME=$(basename "$TRACE_FILE" .csv)
NUM_JOBS=$(wc -l < "$TRACE_FILE" | tr -d ' ')

# Common simulation parameters
TOTAL_NODES=2000
MAX_JOBS=50000
TRACE_FORMAT="simple"
TIMESTAMP_FORMAT="epoch"
DURATION_MODE="exact"
BACKFILL_POLICY="easy"

echo "Parameters:"
echo "  Jobs in trace: $NUM_JOBS"
echo "  Total nodes: $TOTAL_NODES"
echo "  Max jobs: $MAX_JOBS"
echo "  Backfill: $BACKFILL_POLICY"
echo ""

# Run baseline (deque)
echo "=========================================="
echo "1. BASELINE: Deque (default FCFS)"
echo "=========================================="

BASELINE_OUT="/tmp/baseline_deque.csv"
BASELINE_RESOURCES="/tmp/baseline_resources.csv"

echo -n "Running... "
START_TIME=$(date +%s%N)

./build/simulator "$TRACE_FILE" \
    --priority_policy fcfs \
    --total_nodes $TOTAL_NODES \
    --max_jobs $MAX_JOBS \
    --trace_format $TRACE_FORMAT \
    --timestamp_format $TIMESTAMP_FORMAT \
    --duration_mode $DURATION_MODE \
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

    ./build/simulator "$TRACE_FILE" \
        --priority_policy fcfs \
        --queue_impl block \
        --block_size $BLOCK_SIZE \
        --total_nodes $TOTAL_NODES \
        --max_jobs $MAX_JOBS \
        --trace_format $TRACE_FORMAT \
        --timestamp_format $TIMESTAMP_FORMAT \
        --duration_mode $DURATION_MODE \
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

# Summary table
echo "=========================================="
echo "3. SUMMARY: Performance & Correctness"
echo "=========================================="
echo ""

printf "%-12s | %-10s | %-10s | %-10s | %-10s | %-12s\n" \
    "Impl" "Time (s)" "vs Deque" "Slowdown" "Peak Queue" "Correctness"
echo "-------------|------------|------------|------------|------------|-------------"

# Baseline
printf "%-12s | %-10s | %-10s | %-10s | %-10s | %-12s\n" \
    "Deque" "$BASELINE_TIME" "1.00x" "baseline" "$BASELINE_PEAK_QUEUE" "baseline"

# Find best block size
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

    # Mark best
    MARKER=""
    if (( $(echo "$TIME < $BEST_BLOCK_TIME" | bc -l) )); then
        BEST_BLOCK_TIME=$TIME
        BEST_BLOCK_SIZE=$BLOCK_SIZE
        MARKER="*"
    fi

    printf "%-12s | %-10s | %-10s | %-10s | %-10s | %-12s\n" \
        "Block-${BLOCK_SIZE}${MARKER}" "$TIME" "${VS_DEQUE}x" "$SLOWDOWN_STR" "$PEAK" "$CORRECT"
done

echo ""
echo "=========================================="
echo "4. CONCLUSIONS"
echo "=========================================="
echo ""

# Check if all passed
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

if $ALL_PASSED; then
    echo "✓ All block sizes produce identical output (correctness verified)"
else
    echo "✗ Some block sizes produced different output (INVESTIGATION NEEDED)"
fi

echo ""
echo "Best block size: Block-$BEST_BLOCK_SIZE"
echo "  Time: ${BEST_BLOCK_TIME}s"
echo "  vs Deque: $(echo "scale=2; $BEST_BLOCK_TIME / $BASELINE_TIME" | bc)x slower"
echo ""

# Calculate deque advantage
DEQUE_ADVANTAGE=$(echo "scale=0; ($BEST_BLOCK_TIME / $BASELINE_TIME - 1) * 100" | bc)
echo "Deque is ${DEQUE_ADVANTAGE}% faster than best block size"
echo ""

echo "RECOMMENDATION: Use deque (default) for typical HPC workloads"
echo ""

# Cleanup option
echo "Temporary files in /tmp/:"
echo "  baseline_*, block*_*.{csv,txt}"
echo ""
echo "To clean up: rm /tmp/baseline_* /tmp/block*"
