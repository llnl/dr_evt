#!/bin/bash
# Run all 34 comprehensive tests on DR_EVT C++ simulator

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

TRACE_DIR="tests/test_traces/comprehensive"
SIMULATOR="./build/simulator"
TOTAL_NODES=100

# Check if simulator exists
if [ ! -f "$SIMULATOR" ]; then
    echo "Error: Simulator not found: $SIMULATOR"
    echo "Build first: cd build && cmake .. && make"
    exit 1
fi

# Test list - all 34 tests
TESTS=(
    # Original 27 tests
    "01_backfill_allowed"
    "02_backfill_blocked_time"
    "03_backfill_blocked_resources"
    "04_backfill_resource_competition"
    "05_multiple_backfills"
    "06_backfill_out_of_order"
    "07_simultaneous_submit"
    "08_simultaneous_completion"
    "09_simultaneous_submit_complete"
    "10_queue_drain_idle"
    "11_multiple_drains"
    "12_drain_with_backlog"
    "13_consecutive_fcfs"
    "14_fcfs_with_backfill"
    "15_fcfs_partial_overlap"
    "16_starvation_prevention"
    "17_late_large_priority"
    "18_backfill_no_starvation"
    "19_resource_fragmentation"
    "20_fragmentation_recovery"
    "21_sustained_high_load"
    "22_bursty_load"
    "23_mixed_load"
    "24_multiple_running_jobs"
    "25_early_completion_basic"
    "26_early_completion_cascading"
    "27_early_vs_late_completion"

    # New tests (28-34) - Bug detection tests
    "28_simultaneous_completions_backfill"
    "29_large_completion_multiple_backfills"
    "30_fcfs_blocked_backfill_past"
    "31_completion_arrival_with_queue"
    "32_simultaneous_backfill_with_reservation"
    "33_five_simultaneous_events"
    "34_backfill_overallocation"
)

PASSED=0
FAILED=0
MISSING=0

echo "=========================================="
echo "DR_EVT C++ Simulator - Comprehensive Tests"
echo "=========================================="
echo ""

for TEST in "${TESTS[@]}"; do
    INPUT="${TRACE_DIR}/${TEST}.csv"
    EXPECTED="${TRACE_DIR}/${TEST}.expected_output.csv"
    OUTPUT="/tmp/${TEST}.cpp_output.csv"
    SIM_OUT="/tmp/${TEST}.sim.out"

    # Check if input exists
    if [ ! -f "$INPUT" ]; then
        echo "✗ $TEST - Input not found"
        ((MISSING++))
        continue
    fi

    # Check if expected output exists
    if [ ! -f "$EXPECTED" ]; then
        echo "⊘ $TEST - No expected output (skipped)"
        ((MISSING++))
        continue
    fi

    # Check if trace has actual_duration column (for early completion tests)
    HEADER=$(head -1 "$INPUT")
    if echo "$HEADER" | grep -q "actual_duration"; then
        DURATION_MODE="column"
    else
        DURATION_MODE="exact"
    fi

    # Run simulator with resource trace
    "$SIMULATOR" "$INPUT" --total_nodes "$TOTAL_NODES" --trace_format simple \
        --timestamp_format epoch --duration_mode "$DURATION_MODE" --outfile "$SIM_OUT" \
        --backfill_policy easy --resource_trace "${SIM_OUT}_resources.csv" > /dev/null 2>&1

    if [ ! -f "$SIM_OUT" ]; then
        echo "✗ $TEST - Simulator failed to produce output"
        ((FAILED++))
        continue
    fi

    # Convert simulator output to comparable format
    # Simulator output: job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit
    # Expected output: job_id,start_time,end_time
    awk -F, 'NR==1 {print "job_id,start_time,end_time"; next} {print NR-2","$2","$3}' "$SIM_OUT" > "$OUTPUT"

    # Compare job schedules
    diff -w "$EXPECTED" "$OUTPUT" > /dev/null 2>&1
    JOB_MATCH=$?

    # Compare resource traces
    EXPECTED_RESOURCES="${TRACE_DIR}/${TEST}.expected_resources.csv"
    ACTUAL_RESOURCES="${SIM_OUT}_resources.csv"
    RESOURCE_MATCH=0

    if [ -f "$EXPECTED_RESOURCES" ] && [ -f "$ACTUAL_RESOURCES" ]; then
        # Convert C++ format (time,free_nodes,allocated_nodes) to expected format (time,nodes_used,nodes_free)
        # Skip initial idle state (time=0, nodes_used=0) if present
        awk -F, 'NR==1 {print "time,nodes_used,nodes_free"; next}
                 NR==2 && $1=="0" && $3=="0" {next}
                 {print $1","$3","$2}' "$ACTUAL_RESOURCES" > "/tmp/${TEST}.cpp_resources.csv"

        # Compare first 3 columns only (ignore running_jobs column)
        awk -F, '{print $1","$2","$3}' "$EXPECTED_RESOURCES" > "/tmp/${TEST}.expected_resources_compare.csv"

        diff -w "/tmp/${TEST}.expected_resources_compare.csv" "/tmp/${TEST}.cpp_resources.csv" > /dev/null 2>&1
        RESOURCE_MATCH=$?
    fi

    if [ $JOB_MATCH -eq 0 ] && [ $RESOURCE_MATCH -eq 0 ]; then
        echo "✓ $TEST"
        ((PASSED++))
    else
        echo "✗ $TEST - Output mismatch"
        ((FAILED++))

        if [ $JOB_MATCH -ne 0 ]; then
            echo "  Job schedule mismatch:"
            echo "    Expected:"
            head -5 "$EXPECTED" | sed 's/^/      /'
            echo "    Actual:"
            head -5 "$OUTPUT" | sed 's/^/      /'
        fi

        if [ -f "$EXPECTED_RESOURCES" ] && [ $RESOURCE_MATCH -ne 0 ]; then
            echo "  Resource trace mismatch:"
            echo "    Expected:"
            head -10 "/tmp/${TEST}.expected_resources_compare.csv" | sed 's/^/      /'
            echo "    Actual:"
            head -10 "/tmp/${TEST}.cpp_resources.csv" | sed 's/^/      /'
        fi
        echo ""
    fi

    # Cleanup intermediate files
    rm -f "$SIM_OUT"
done

echo ""
echo "=========================================="
echo "RESULTS"
echo "=========================================="
echo "Passed:  $PASSED"
echo "Failed:  $FAILED"
echo "Missing: $MISSING"
echo "Total:   ${#TESTS[@]}"
echo ""

if [ $FAILED -eq 0 ] && [ $MISSING -eq 0 ]; then
    echo "🎉 ALL TESTS PASSED!"
    exit 0
elif [ $FAILED -eq 0 ]; then
    echo "⚠️  All available tests passed, but some expected outputs missing"
    exit 0
else
    echo "❌ SOME TESTS FAILED"
    exit 1
fi
