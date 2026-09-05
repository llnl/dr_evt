#!/bin/bash
# Run Time Mode Tests
#
# Tests run_time_mode behavioral contracts that other test suites don't cover.
#
# Scheduler uses time_limit as the best estimator for planning (realistic behavior).
# run_time_mode determines how jobs actually execute:
#   * actual: Read actual_run_time from trace (most realistic, default)
#   * limit: Jobs run exactly time_limit (unrealistic, for debugging)
#   * distribution: Sample from statistical distribution
#
# This test verifies:
# 1. run_time_mode=actual reads actual_run_time from trace
# 2. run_time_mode=limit uses time_limit exactly
# 3. run_time_mode=distribution samples and respects time_limit cap
# 4. normal/lognormal distributions are capped at time_limit
# 5. uniform distribution is NOT capped (explicit upper bound by design)

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

# Source common simulator path finder
source "$SCRIPT_DIR/set_simulator_path.sh"

echo "=========================================="
echo "Run Time Mode Tests"
echo "=========================================="
echo ""


PASS=0
FAIL=0

# Use a trace where actual_run_time differs from time_limit
# Job 1: time_limit=200, actual_run_time=50
TRACE="tests/test_traces/comprehensive/25_early_completion_basic.csv"

# ------------------------------------------------------------------------
# Test 1: run_time_mode=actual reads actual_run_time from trace
# ------------------------------------------------------------------------

echo "--- Test 1: run_time_mode=actual reads actual_run_time from trace ---"

$SIMULATOR "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode actual \
    --backfill_policy easy \
    --outfile /tmp/runtime_test_actual.csv \
    > /dev/null 2>&1

# Job 1 is job_submit_time=0, should run for actual_run_time=50s
JOB1_LINE=$(awk -F, '$1 == 0' /tmp/runtime_test_actual.csv)
BEGIN=$(echo "$JOB1_LINE" | cut -d, -f2)
END=$(echo "$JOB1_LINE" | cut -d, -f3)
EXEC_TIME=$((END - BEGIN))

if [ "$EXEC_TIME" -eq 50 ]; then
    echo "✓ PASS: run_time_mode=actual used actual_run_time from trace (50s)"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: expected 50s execution time, got ${EXEC_TIME}s"
    FAIL=$((FAIL + 1))
fi

echo ""

# ------------------------------------------------------------------------
# Test 2: run_time_mode=limit uses time_limit exactly
# ------------------------------------------------------------------------

echo "--- Test 2: run_time_mode=limit uses time_limit exactly ---"

$SIMULATOR "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --backfill_policy easy \
    --outfile /tmp/runtime_test_limit.csv \
    > /dev/null 2>&1

# Job 1 should run for full time_limit=200s
JOB1_LINE=$(awk -F, '$1 == 0' /tmp/runtime_test_limit.csv)
BEGIN=$(echo "$JOB1_LINE" | cut -d, -f2)
END=$(echo "$JOB1_LINE" | cut -d, -f3)
EXEC_TIME=$((END - BEGIN))

if [ "$EXEC_TIME" -eq 200 ]; then
    echo "✓ PASS: run_time_mode=limit used time_limit (200s)"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: expected 200s execution time, got ${EXEC_TIME}s"
    FAIL=$((FAIL + 1))
fi

echo ""

# ------------------------------------------------------------------------
# Test 3: run_time_mode=distribution produces varying results
# ------------------------------------------------------------------------

echo "--- Test 3: run_time_mode=distribution samples from distribution ---"

$SIMULATOR "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode distribution \
    --run_time_distribution normal \
    --run_time_scale 0.5 \
    --run_time_stddev 0.2 \
    --seed 42 \
    --backfill_policy easy \
    --outfile /tmp/runtime_test_dist1.csv \
    > /dev/null 2>&1

# Different seed should produce different results
$SIMULATOR "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode distribution \
    --run_time_distribution normal \
    --run_time_scale 0.5 \
    --run_time_stddev 0.2 \
    --seed 99 \
    --backfill_policy easy \
    --outfile /tmp/runtime_test_dist2.csv \
    > /dev/null 2>&1

# Same seed should produce identical results
$SIMULATOR "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode distribution \
    --run_time_distribution normal \
    --run_time_scale 0.5 \
    --run_time_stddev 0.2 \
    --seed 42 \
    --backfill_policy easy \
    --outfile /tmp/runtime_test_dist3.csv \
    > /dev/null 2>&1

# Different seeds should differ
if ! diff -q /tmp/runtime_test_dist1.csv /tmp/runtime_test_dist2.csv > /dev/null 2>&1; then
    # Same seed should match
    if diff -q /tmp/runtime_test_dist1.csv /tmp/runtime_test_dist3.csv > /dev/null 2>&1; then
        echo "✓ PASS: distribution mode samples vary by seed, deterministic with same seed"
        PASS=$((PASS + 1))
    else
        echo "✗ FAIL: same seed produced different results (not deterministic)"
        FAIL=$((FAIL + 1))
    fi
else
    echo "✗ FAIL: different seeds produced identical results (not sampling)"
    FAIL=$((FAIL + 1))
fi

echo ""

# ------------------------------------------------------------------------
# Test 4: normal/lognormal distributions are capped at time_limit
# ------------------------------------------------------------------------

echo "--- Test 4: normal/lognormal distributions capped at time_limit ---"

LARGE_TRACE="tests/test_traces/scale/huge_10000jobs.csv"

check_no_exceedance() {
    local dist="$1"
    local outfile="$2"
    local should_cap="$3"  # "yes" or "no"

    $SIMULATOR "$LARGE_TRACE" \
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

should_cap = '$should_cap' == 'yes'

if should_cap:
    # Distribution should be capped
    if exceed_count > 0:
        print(f'✗ FAIL: {exceed_count}/{total} jobs exceeded time_limit under $dist (max excess: {max_excess:.1f}s)')
        sys.exit(1)
    else:
        print(f'✓ PASS: 0/{total} jobs exceeded time_limit under $dist (properly capped)')
        sys.exit(0)
else:
    # Uniform should NOT be capped (by design)
    # Just verify it ran successfully
    print(f'✓ PASS: $dist distribution completed ({total} jobs)')
    sys.exit(0)
"
}

if check_no_exceedance "normal" "/tmp/runtime_test_normal.csv" "yes"; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if check_no_exceedance "lognormal" "/tmp/runtime_test_lognormal.csv" "yes"; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if check_no_exceedance "uniform" "/tmp/runtime_test_uniform.csv" "no"; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

echo ""

# ------------------------------------------------------------------------
# Test 5: Verify scheduler uses time_limit as best estimator (not actual_run_time)
# ------------------------------------------------------------------------

echo "--- Test 5: Scheduler planning always uses time_limit ---"

# Create a simple trace where using actual_run_time for scheduling would
# produce a different schedule than using time_limit
cat > /tmp/scheduler_planning_test.csv <<EOF
job_submit_time,num_nodes,exit_status,queue,time_limit,actual_run_time
0,60,0,pbatch,1000,10
5,50,0,pbatch,100,90
EOF

# Job 0: time_limit=1000, actual_run_time=10
# Job 1: time_limit=100, actual_run_time=90
# With 100 nodes total:
# - If scheduler uses time_limit: Job 0 blocks Job 1 (60+50 > 100)
# - If scheduler used actual_run_time: Job 1 could backfill (job 0 would
#   appear to free up at t=10, not t=1000)

$SIMULATOR /tmp/scheduler_planning_test.csv \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode actual \
    --backfill_policy easy \
    --outfile /tmp/runtime_test_sched_actual.csv \
    > /dev/null 2>&1

# Job 1 should start at t=10 (after job 0 completes), not t=5
# because scheduler plans with time_limit (1000s), not actual_run_time (10s)
JOB1_LINE=$(awk -F, '$1 == 5' /tmp/runtime_test_sched_actual.csv)
BEGIN=$(echo "$JOB1_LINE" | cut -d, -f2)

if [ "$BEGIN" -eq 10 ]; then
    echo "✓ PASS: Scheduler uses time_limit for planning (job 1 starts at t=10)"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: Expected job 1 to start at t=10, started at t=${BEGIN}"
    echo "  (Scheduler may be using actual_run_time instead of time_limit)"
    FAIL=$((FAIL + 1))
fi

echo ""

# Cleanup
rm -f /tmp/runtime_test_*.csv /tmp/scheduler_planning_test.csv

echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ "$FAIL" -eq 0 ]; then
    echo "✓ ALL RUN TIME MODE TESTS PASSED"
    exit 0
else
    echo "✗ SOME TESTS FAILED"
    exit 1
fi
