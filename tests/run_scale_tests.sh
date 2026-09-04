#!/bin/bash
# Scale Tests - Simulation Mode (Optional)
#
# Tests scheduler performance at scale:
# - 10 to 2000 jobs
# - Compares against expected outputs
#
# These are marked optional in CI but test actual scheduling.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "=========================================="
echo "Scale Tests (Simulation Mode - Optional)"
echo "=========================================="
echo ""

if [ ! -f "${SIMULATOR:-./build/simulator}" ]; then
    echo "Error: ./build/simulator not found"
    exit 1
fi

PASS=0
FAIL=0

TRACE_DIR="tests/test_traces/scale"

for test_file in "$TRACE_DIR"/*.csv; do
    # Skip expected files
    if [[ "$test_file" == *.expected* ]]; then
        continue
    fi

    test_name=$(basename "$test_file" .csv)
    echo "Testing: $test_name"

    EXPECTED="$TRACE_DIR/${test_name}.expected_output.csv"

    if [ ! -f "$EXPECTED" ]; then
        echo "  ⚠ SKIP - No expected output"
        continue
    fi

    SIM_OUT="/tmp/scale_${test_name}.csv"
    SIM_RESOURCES="/tmp/scale_${test_name}_resources.csv"
    rm -f "$SIM_OUT"

    # scale/ jobs require 795 nodes (matches src/dr_evt_types.hpp's
    # default), NOT 100 - several jobs request up to 497 nodes and
    # cannot be scheduled at all under 100 nodes. Using --total_nodes
    # 100 here previously caused jobs to silently never start, and
    # made every scale test fail against expected_output.csv files
    # that were generated at 795 nodes.
    #
    # `|| true` prevents a simulator crash (e.g. an unrecognized flag,
    # or any other nonzero exit) from aborting this entire script under
    # set -e - the check below (SIM_OUT missing) still catches that
    # failure and reports it as this test's own failure, rather than
    # silently skipping every test after it.
    # --duration_mode isn't passed, so it stays at its default, "limit".
    # That matters because duration_mode="actual" would make the
    # scheduler ignore --run_time_mode entirely and just use the
    # trace's own real run time - "limit" is what makes --run_time_mode
    # exact below actually get used.
    ./build/simulator "$test_file" \
        --total_nodes 795 \
        --trace_format simple \
        --timestamp_format epoch \
        --run_time_mode exact \
        --backfill_policy easy \
        --priority_policy fcfs \
        --outfile "$SIM_OUT" \
        --resource_trace "$SIM_RESOURCES" > /dev/null 2>&1 || true

    if [ ! -f "$SIM_OUT" ]; then
        echo "  ✗ FAIL - Simulator did not produce output"
        FAIL=$((FAIL + 1))
        continue
    fi

    OUTPUT="/tmp/scale_${test_name}_comparable.csv"
    awk -F, 'NR==1 {print "job_id,start_time,end_time"; next} {print NR-2","$2","$3}' "$SIM_OUT" > "$OUTPUT"

    # -w: ignore whitespace differences (e.g. line-ending variations
    # between how expected files and simulator output happen to be
    # written) rather than treating them as a genuine schedule mismatch.
    if diff -w "$EXPECTED" "$OUTPUT" > /dev/null 2>&1; then
        echo "  ✓ PASS"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL - Schedule mismatch"
        echo "     Expected (first 5 lines):"
        head -5 "$EXPECTED" | sed 's/^/       /'
        echo "     Actual (first 5 lines):"
        head -5 "$OUTPUT" | sed 's/^/       /'
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL SCALE TESTS PASSED"
    exit 0
else
    echo "✗ SOME SCALE TESTS FAILED"
    exit 1
fi
