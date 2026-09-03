#!/bin/bash
# Duration/Run Time Mode Tests
#
# Tests two specific behavioral contracts that no other test suite covers.
#
# Terminology: duration_mode is the scheduler's own job-length estimate
# for reservation/backfill planning (limit/actual) - what the scheduler
# considers. run_time_mode is how the job's actual, observed execution
# length is determined (exact/column/distribution) - what's observed
# when the job runs.
#
# 1. duration_mode=actual ignores run_time_mode entirely, using the trace's
#    own real, historical actual_run_time directly - not a run_time_mode-
#    determined value (exact/column/distribution). Fixed in
#    Simulation::determine_job_run_time(): previously, duration_mode=actual
#    combined with run_time_mode=exact would silently overwrite a job's
#    real historical run time with its time_limit, which made no sense for
#    a mode whose entire purpose is "use what actually happened."
#
# 2. sample_run_time()'s normal/lognormal distributions are capped at
#    time_limit - a real HPC scheduler kills a job at its stated limit, so
#    the simulator must respect the same constraint or its output diverges
#    from real system behavior. uniform is deliberately NOT capped: its
#    upper bound is already an explicit, direct function of the caller's
#    own scale/stddev choice, not an unbounded-tail artifact like
#    normal/lognormal.
#
# Neither of these is exercised by test_fcfs_comprehensive.sh (which
# differentially tests queue implementations, not run time semantics) or
# test_all_dr_evt.sh (which never passes --run_time_mode or
# --duration_mode=actual at all).

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "=========================================="
echo "Duration/Run Time Mode Tests"
echo "=========================================="
echo ""

if [ ! -f "./build/simulator" ]; then
    echo "Error: ./build/simulator not found"
    echo "Please build first: cd build && cmake .. && make"
    exit 1
fi

PASS=0
FAIL=0

# ------------------------------------------------------------------------
# Test 1: duration_mode=actual ignores run_time_mode
# ------------------------------------------------------------------------
# tests/test_traces/comprehensive/25_early_completion_basic.csv has jobs
# where actual_run_time differs from time_limit (job 1: time_limit=200,
# actual_run_time=50) - exactly the case needed to distinguish "used the
# real historical run time" from "used run_time_mode's own logic".

echo "--- Test 1: duration_mode=actual ignores run_time_mode ---"

TRACE="tests/test_traces/comprehensive/25_early_completion_basic.csv"

# run_time_mode=exact would normally overwrite actual_run_time with
# time_limit - duration_mode=actual must prevent that from happening.
./build/simulator "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode exact \
    --duration_mode actual \
    --backfill_policy easy \
    --outfile /tmp/duration_test_actual_exact.csv \
    > /dev/null 2>&1

# run_time_mode=distribution should be ignored identically - if
# duration_mode=actual is doing its job, this must match the exact-mode
# run above byte-for-byte, since run_time_mode never gets consulted.
./build/simulator "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode distribution \
    --run_time_scale 0.5 \
    --seed 99 \
    --duration_mode actual \
    --backfill_policy easy \
    --outfile /tmp/duration_test_actual_dist.csv \
    > /dev/null 2>&1

if diff -q /tmp/duration_test_actual_exact.csv /tmp/duration_test_actual_dist.csv > /dev/null 2>&1; then
    # Also confirm the actual value used was the trace's real 50s, not
    # time_limit's 200s - job 1 is job_submit_time=0 in this trace.
    JOB1_LINE=$(awk -F, '$1 == 0' /tmp/duration_test_actual_exact.csv)
    BEGIN=$(echo "$JOB1_LINE" | cut -d, -f2)
    END=$(echo "$JOB1_LINE" | cut -d, -f3)
    EXEC_TIME=$((END - BEGIN))
    if [ "$EXEC_TIME" -eq 50 ]; then
        echo "✓ PASS: duration_mode=actual used the trace's real run time (50s), ignoring run_time_mode"
        PASS=$((PASS + 1))
    else
        echo "✗ FAIL: expected 50s execution time, got ${EXEC_TIME}s"
        FAIL=$((FAIL + 1))
    fi
else
    echo "✗ FAIL: run_time_mode=exact and run_time_mode=distribution produced different output under duration_mode=actual (run_time_mode should be ignored entirely)"
    diff /tmp/duration_test_actual_exact.csv /tmp/duration_test_actual_dist.csv | head -10
    FAIL=$((FAIL + 1))
fi

# Contrast case: duration_mode=limit (default) with run_time_mode=exact
# should use the full time_limit (200s), not the real actual_run_time -
# confirms the two modes still behave genuinely differently.
./build/simulator "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode exact \
    --duration_mode limit \
    --backfill_policy easy \
    --outfile /tmp/duration_test_limit_exact.csv \
    > /dev/null 2>&1

JOB1_LINE=$(awk -F, '$1 == 0' /tmp/duration_test_limit_exact.csv)
BEGIN=$(echo "$JOB1_LINE" | cut -d, -f2)
END=$(echo "$JOB1_LINE" | cut -d, -f3)
EXEC_TIME=$((END - BEGIN))
if [ "$EXEC_TIME" -eq 200 ]; then
    echo "✓ PASS: duration_mode=limit with run_time_mode=exact used time_limit (200s), confirming the two modes genuinely differ"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: expected 200s execution time under duration_mode=limit, got ${EXEC_TIME}s"
    FAIL=$((FAIL + 1))
fi

echo ""

# ------------------------------------------------------------------------
# Test 2: normal/lognormal distributions are capped at time_limit
# ------------------------------------------------------------------------
# Uses a high-variance distribution on a large trace to reliably produce
# samples that would exceed time_limit if uncapped - a small trace could
# pass by chance even with the bug present.

echo "--- Test 2: normal/lognormal distributions capped at time_limit ---"

LARGE_TRACE="tests/test_traces/scale/huge_10000jobs.csv"

check_no_exceedance() {
    local dist="$1"
    local outfile="$2"

    ./build/simulator "$LARGE_TRACE" \
        --priority_policy fcfs \
        --total_nodes 500 \
        --trace_format simple \
        --timestamp_format epoch \
        --run_time_mode distribution \
        --run_time_distribution "$dist" \
        --run_time_scale 1.0 \
        --run_time_stddev 0.6 \
        --seed 7 \
        --backfill_policy easy \
        --outfile "$outfile" \
        > /dev/null 2>&1

    python3 -c "
import csv
import sys

exceed_count = 0
total = 0
max_excess = 0.0

with open('$outfile') as f:
    reader = csv.DictReader(f)
    for row in reader:
        begin = float(row['begin_time'])
        end = float(row['end_time'])
        limit = float(row['time_limit'])
        if begin < -1e18:
            continue  # sentinel: job never ran
        actual_exec = end - begin
        total += 1
        # small tolerance for floating point/integer-second rounding
        if actual_exec > limit + 1.0:
            exceed_count += 1
            max_excess = max(max_excess, actual_exec - limit)

if exceed_count > 0:
    print(f'✗ FAIL: {exceed_count}/{total} jobs exceeded time_limit under $dist (max excess: {max_excess:.1f}s)')
    sys.exit(1)
else:
    print(f'✓ PASS: 0/{total} jobs exceeded time_limit under $dist')
    sys.exit(0)
"
}

if check_no_exceedance "normal" "/tmp/duration_test_normal.csv"; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if check_no_exceedance "lognormal" "/tmp/duration_test_lognormal.csv"; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

echo ""

# Cleanup
rm -f /tmp/duration_test_*.csv

echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ "$FAIL" -eq 0 ]; then
    echo "✓ ALL DURATION/RUN TIME MODE TESTS PASSED"
    exit 0
else
    echo "✗ SOME TESTS FAILED"
    exit 1
fi
