#!/bin/bash
# Unit Tests - Simulation Mode with Reference Comparison
#
# Tests basic functionality with simulation mode:
# - Timestamp formats (epoch, ISO)
# - Timezone handling
# - Simple job traces
#
# Each test compares simulator output against expected reference files.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

# Source common simulator path finder
source "$SCRIPT_DIR/set_simulator_path.sh"

echo "=========================================="
echo "Unit Tests (Simulation Mode)"
echo "=========================================="
echo ""

PASS=0
FAIL=0

TRACE_DIR="tests/test_traces/unit"

# Run each unit test
for test_file in "$TRACE_DIR"/*.csv; do
    # Skip expected files
    if [[ "$test_file" == *.expected* ]]; then
        continue
    fi

    test_name=$(basename "$test_file" .csv)
    echo "Testing: $test_name"

    EXPECTED="$TRACE_DIR/${test_name}.expected_output.csv"

    # Check if expected file exists
    if [ ! -f "$EXPECTED" ]; then
        echo "  ⚠ SKIP - No expected output file"
        continue
    fi

    # Run simulator
    SIM_OUT="/tmp/unit_${test_name}.csv"
    SIM_RESOURCES="/tmp/unit_${test_name}_resources.csv"

    # These test traces lack actual_run_time column, so use run_time_mode=limit
    $SIMULATOR "$test_file" \
        --total_nodes 100 \
        --trace_format simple \
        --timestamp_format epoch \
        --run_time_mode limit \
        --backfill_policy easy \
        --priority_policy fcfs \
        --outfile "$SIM_OUT" \
        --resource_trace "$SIM_RESOURCES" > /dev/null 2>&1

    if [ ! -f "$SIM_OUT" ]; then
        echo "  ✗ FAIL - Simulator did not produce output"
        FAIL=$((FAIL + 1))
        continue
    fi

    # Convert simulator output to comparable format
    # Simulator: job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit
    # Expected: job_id,start_time,end_time
    OUTPUT="/tmp/unit_${test_name}_comparable.csv"
    awk -F, 'NR==1 {print "job_id,start_time,end_time"; next} {print NR-2","$2","$3}' "$SIM_OUT" > "$OUTPUT"

    # Compare job schedules
    if diff -w "$EXPECTED" "$OUTPUT" > /dev/null 2>&1; then
        JOB_MATCH=0
    else
        JOB_MATCH=1
    fi

    # Compare resource traces if expected exists
    EXPECTED_RESOURCES="$TRACE_DIR/${test_name}.expected_resources.csv"
    RESOURCE_MATCH=0

    if [ -f "$EXPECTED_RESOURCES" ]; then
        # Convert C++ format to expected format
        awk -F, 'NR==1 {print "time,nodes_used,nodes_free"; next}
                 NR==2 && $1=="0" && $3=="0" {next}
                 {print $1","$3","$2}' "$SIM_RESOURCES" > "/tmp/unit_${test_name}_resources_comparable.csv"

        if ! diff -w "$EXPECTED_RESOURCES" "/tmp/unit_${test_name}_resources_comparable.csv" > /dev/null 2>&1; then
            RESOURCE_MATCH=1
        fi
    fi

    # Report results
    if [ $JOB_MATCH -eq 0 ] && [ $RESOURCE_MATCH -eq 0 ]; then
        echo "  ✓ PASS"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL"
        if [ $JOB_MATCH -ne 0 ]; then
            echo "     Job schedule mismatch"
        fi
        if [ $RESOURCE_MATCH -ne 0 ]; then
            echo "     Resource trace mismatch"
        fi
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL UNIT TESTS PASSED"
    exit 0
else
    echo "✗ SOME UNIT TESTS FAILED"
    exit 1
fi
