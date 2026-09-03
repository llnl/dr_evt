#!/bin/bash
# Feature Tests - Simulation Mode with Reference Comparison
#
# Tests specific features/policies:
# - Conservative vs EASY backfilling
# - Different scheduling policies
#
# Each test compares simulator output against expected reference files.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "=========================================="
echo "Feature Tests (Simulation Mode)"
echo "=========================================="
echo ""

if [ ! -f "./build/simulator" ]; then
    echo "Error: ./build/simulator not found"
    echo "Build first: cd build && cmake .. && make"
    exit 1
fi

PASS=0
FAIL=0

TRACE_DIR="tests/test_traces/feature"

for test_file in "$TRACE_DIR"/*.csv; do
    # Skip expected files
    if [[ "$test_file" == *.expected* ]]; then
        continue
    fi

    test_name=$(basename "$test_file" .csv)
    echo "Testing: $test_name"

    EXPECTED="$TRACE_DIR/${test_name}.expected_output.csv"

    if [ ! -f "$EXPECTED" ]; then
        echo "  ⚠ SKIP - No expected output file"
        continue
    fi

    # Optional companion file: extra CLI args for this specific test only
    # (e.g. --msec_output). Absent for every existing feature test, so
    # this is purely additive - no effect on tests that don't have one.
    EXTRA_ARGS=()
    FLAGS_FILE="$TRACE_DIR/${test_name}.flags"
    if [ -f "$FLAGS_FILE" ]; then
        # Word-split intentionally: the .flags file holds space-separated
        # CLI arguments, not a single opaque string.
        read -r -a EXTRA_ARGS < "$FLAGS_FILE"
    fi

    # Run simulator
    SIM_OUT="/tmp/feature_${test_name}.csv"
    SIM_RESOURCES="/tmp/feature_${test_name}_resources.csv"

    # duration_mode controls whether run_time_mode matters at all:
    #   - duration_mode="actual" makes the scheduler ignore run_time_mode
    #     completely and just use the trace's own real, historical
    #     run time.
    #   - duration_mode="limit" (the default) is what makes run_time_mode
    #     get consulted in the first place.
    # This script never passes --duration_mode, and no current .flags
    # file does either, so it stays at "limit" - meaning the
    # --run_time_mode exact below is not a no-op, it's actually used.
    ./build/simulator "$test_file" \
        --total_nodes 100 \
        --trace_format simple \
        --timestamp_format epoch \
        --run_time_mode exact \
        --backfill_policy easy \
        --priority_policy fcfs \
        "${EXTRA_ARGS[@]}" \
        --outfile "$SIM_OUT" \
        --resource_trace "$SIM_RESOURCES" > /dev/null 2>&1 || true

    if [ ! -f "$SIM_OUT" ]; then
        echo "  ✗ FAIL - Simulator did not produce output"
        FAIL=$((FAIL + 1))
        continue
    fi

    # Convert simulator output to comparable format
    OUTPUT="/tmp/feature_${test_name}_comparable.csv"
    awk -F, 'NR==1 {print "job_id,start_time,end_time"; next} {print NR-2","$2","$3}' "$SIM_OUT" > "$OUTPUT"

    # Compare job schedules
    if diff -w "$EXPECTED" "$OUTPUT" > /dev/null 2>&1; then
        JOB_MATCH=0
    else
        JOB_MATCH=1
    fi

    # Compare resource traces
    EXPECTED_RESOURCES="$TRACE_DIR/${test_name}.expected_resources.csv"
    RESOURCE_MATCH=0

    if [ -f "$EXPECTED_RESOURCES" ]; then
        awk -F, 'NR==1 {print "time,nodes_used,nodes_free"; next}
                 NR==2 && $1=="0" && $3=="0" {next}
                 {print $1","$3","$2}' "$SIM_RESOURCES" > "/tmp/feature_${test_name}_resources_comparable.csv"

        if ! diff -w "$EXPECTED_RESOURCES" "/tmp/feature_${test_name}_resources_comparable.csv" > /dev/null 2>&1; then
            RESOURCE_MATCH=1
        fi
    fi

    # Report results
    if [ $JOB_MATCH -eq 0 ] && [ $RESOURCE_MATCH -eq 0 ]; then
        echo "  ✓ PASS"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL"
        [ $JOB_MATCH -ne 0 ] && echo "     Job schedule mismatch"
        [ $RESOURCE_MATCH -ne 0 ] && echo "     Resource trace mismatch"
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL FEATURE TESTS PASSED"
    exit 0
else
    echo "✗ SOME FEATURE TESTS FAILED"
    exit 1
fi
