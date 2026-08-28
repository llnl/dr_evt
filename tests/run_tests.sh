#!/bin/bash
#
# DR_EVT Scheduler Test Suite
#
# Tests SLURM-style backfilling scheduler implementation
# Run from project root: ./tests/run_tests.sh
#

set -e  # Exit on first error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SIMULATOR="./build/simulator"
TEST_DIR="./test_traces"
RESULTS_DIR="./tests/results"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Check if simulator exists
if [ ! -x "$SIMULATOR" ]; then
    echo -e "${RED}ERROR: Simulator not found at $SIMULATOR${NC}"
    echo "Please build the project first: cd build && make"
    exit 1
fi

# Check if test traces exist
if [ ! -d "$TEST_DIR" ]; then
    echo -e "${RED}ERROR: Test traces directory not found at $TEST_DIR${NC}"
    exit 1
fi

echo "========================================"
echo "  DR_EVT Scheduler Test Suite"
echo "========================================"
echo ""

# Function to run a test
run_test() {
    local test_name="$1"
    local trace_file="$2"
    local expected_jobs="$3"
    local expected_completed="$4"
    local nodes="$5"
    local extra_args="$6"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    echo -n "Running: $test_name ... "

    # Run simulator and capture output
    output=$($SIMULATOR "$trace_file" --total_nodes "$nodes" \
        --backfill_policy easy --priority_policy fcfs --runtime_mode actual \
        $extra_args 2>&1)

    # Extract key metrics
    jobs_submitted=$(echo "$output" | grep "Jobs submitted:" | tail -1 | awk '{print $3}')
    jobs_completed=$(echo "$output" | grep "Jobs completed:" | tail -1 | awk '{print $3}')

    # Check if test passed
    if [ "$jobs_submitted" == "$expected_jobs" ] && [ "$jobs_completed" == "$expected_completed" ]; then
        echo -e "${GREEN}PASS${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))

        # Save output
        echo "$output" > "$RESULTS_DIR/${test_name}.log"
    else
        echo -e "${RED}FAIL${NC}"
        echo "  Expected: $expected_jobs submitted, $expected_completed completed"
        echo "  Got: $jobs_submitted submitted, $jobs_completed completed"
        FAILED_TESTS=$((FAILED_TESTS + 1))

        # Save output for debugging
        echo "$output" > "$RESULTS_DIR/${test_name}_FAILED.log"
    fi
}

# Function to check resource tracking
check_resources() {
    local test_name="$1"
    local trace_file="$2"
    local nodes="$3"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    echo -n "Running: $test_name ... "

    # Run simulator and capture output
    output=$($SIMULATOR "$trace_file" --total_nodes "$nodes" \
        --backfill_policy easy --priority_policy fcfs --runtime_mode actual \
        --trace_format simple --timestamp_format epoch 2>&1)

    # Check if resources return to full capacity at end
    final_free=$(echo "$output" | grep "Resources freed" | tail -1 | grep -o "[0-9]*/100 free" | cut -d'/' -f1)

    if [ "$final_free" == "$nodes" ]; then
        echo -e "${GREEN}PASS${NC} (all resources returned)"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo "$output" > "$RESULTS_DIR/${test_name}.log"
    else
        echo -e "${RED}FAIL${NC}"
        echo "  Expected: $nodes free nodes at end"
        echo "  Got: $final_free free nodes"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "$output" > "$RESULTS_DIR/${test_name}_FAILED.log"
    fi
}

echo "=== Basic Functionality Tests ==="
echo ""

run_test "basic_sequential" \
    "$TEST_DIR/epoch_pbatch.csv" \
    3 3 100 \
    "--trace_format simple --timestamp_format epoch"

run_test "concurrent_backfill" \
    "$TEST_DIR/backfill_test.csv" \
    3 3 100 \
    "--trace_format simple --timestamp_format epoch"

echo ""
echo "=== Resource Management Tests ==="
echo ""

check_resources "resource_return" \
    "$TEST_DIR/backfill_test.csv" \
    100

check_resources "resource_saturation" \
    "$TEST_DIR/saturation_test.csv" \
    100

echo ""
echo "=== Backfill Behavior Tests ==="
echo ""

run_test "backfill_success" \
    "$TEST_DIR/backfill_window_success.csv" \
    2 2 100 \
    "--trace_format simple --timestamp_format epoch"

run_test "backfill_idle_resources" \
    "$TEST_DIR/idle_resources.csv" \
    3 3 100 \
    "--trace_format simple --timestamp_format epoch"

run_test "backfill_partial" \
    "$TEST_DIR/backfill_blocked.csv" \
    3 3 100 \
    "--trace_format simple --timestamp_format epoch"

echo ""
echo "=== Policy Tests ==="
echo ""

run_test "conservative_backfill" \
    "$TEST_DIR/conservative_backfill.csv" \
    3 3 100 \
    "--trace_format simple --timestamp_format epoch --backfill_policy conservative"

run_test "priority_sjf" \
    "$TEST_DIR/priority_sjf.csv" \
    3 3 100 \
    "--trace_format simple --timestamp_format epoch --priority_policy sjf"

run_test "priority_ljf" \
    "$TEST_DIR/priority_ljf.csv" \
    3 3 100 \
    "--trace_format simple --timestamp_format epoch --priority_policy ljf"

run_test "runtime_limit" \
    "$TEST_DIR/runtime_limit.csv" \
    3 3 100 \
    "--trace_format simple --timestamp_format epoch --runtime_mode limit"

echo ""
echo "=== Format Tests ==="
echo ""

run_test "iso_timestamps" \
    "$TEST_DIR/iso_timestamps.csv" \
    3 3 100 \
    "--trace_format simple --timestamp_format iso"

run_test "timezone_offsets" \
    "$TEST_DIR/timezone_offsets.csv" \
    3 3 100 \
    "--trace_format simple --timestamp_format iso"

echo ""
echo "=== Stress Tests ==="
echo ""

run_test "saturation_30jobs" \
    "$TEST_DIR/saturation_test.csv" \
    30 30 100 \
    "--trace_format simple --timestamp_format epoch"

run_test "large_scale_100jobs" \
    "$TEST_DIR/large_scale_100jobs.csv" \
    100 100 100 \
    "--trace_format simple --timestamp_format epoch --max_jobs 100 --runtime_mode actual"

echo ""
echo "========================================"
echo "  Test Summary"
echo "========================================"
echo "Total tests:  $TOTAL_TESTS"
echo -e "Passed:       ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed:       ${RED}$FAILED_TESTS${NC}"
echo ""

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    echo ""
    echo "Test results saved to: $RESULTS_DIR/"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    echo ""
    echo "Failed test logs saved to: $RESULTS_DIR/*_FAILED.log"
    exit 1
fi
