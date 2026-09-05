#!/bin/bash
# Column Alias Tests
#
# Tests that time_limit and actual_run_time each accept multiple column-name
# aliases in "simple"-format traces, so an existing trace file can be
# reused without editing its header (slow to do by hand on a large file).
# Added in src/trace/data_columns.cpp's find_column()/find_column_optional().
#
# time_limit accepts: time_limit, timelimit, walltime
# actual_run_time accepts: actual_run_time, duration, actual_duration, run_time
#
# No other test suite exercises any alias other than the canonical name.
#
# Tests verify that column name aliases work correctly with run_time_mode.
# Uses --run_time_mode limit for traces without actual_run_time column,
# and --run_time_mode actual for traces with that column.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

# Source common simulator path finder
source "$SCRIPT_DIR/set_simulator_path.sh"

echo "=========================================="
echo "Column Alias Tests"
echo "=========================================="
echo ""


PASS=0
FAIL=0

# ------------------------------------------------------------------------
# time_limit aliases
# ------------------------------------------------------------------------
echo "--- time_limit column aliases ---"

for alias in time_limit timelimit walltime; do
    cat > "/tmp/alias_tl_${alias}.csv" << EOF
job_submit_time,num_nodes,exit_status,queue,${alias}
0,70,0,pbatch,200
EOF

    $SIMULATOR "/tmp/alias_tl_${alias}.csv" \
        --priority_policy fcfs \
        --total_nodes 100 \
        --trace_format simple \
        --timestamp_format epoch \
        --run_time_mode limit \
        --backfill_policy easy \
        --outfile "/tmp/alias_tl_${alias}_out.csv" \
        > "/tmp/alias_tl_${alias}_err.txt" 2>&1

    if [ $? -eq 0 ]; then
        # run_time_mode=limit should use the column's value (200) as the
        # job's execution time, regardless of which alias named it.
        LINE=$(awk -F, '$1 == 0' "/tmp/alias_tl_${alias}_out.csv")
        BEGIN=$(echo "$LINE" | cut -d, -f2)
        END=$(echo "$LINE" | cut -d, -f3)
        EXEC_TIME=$((END - BEGIN))
        if [ "$EXEC_TIME" -eq 200 ]; then
            echo "✓ PASS: '$alias' recognized as time_limit (execution time 200s)"
            PASS=$((PASS + 1))
        else
            echo "✗ FAIL: '$alias' recognized but wrong execution time: ${EXEC_TIME}s (expected 200s)"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "✗ FAIL: '$alias' not recognized as time_limit"
        cat "/tmp/alias_tl_${alias}_err.txt"
        FAIL=$((FAIL + 1))
    fi
done

echo ""

# ------------------------------------------------------------------------
# actual_run_time aliases
# ------------------------------------------------------------------------
echo "--- actual_run_time column aliases (run_time_mode=actual) ---"

for alias in actual_run_time duration actual_duration run_time; do
    cat > "/tmp/alias_ar_${alias}.csv" << EOF
job_submit_time,num_nodes,exit_status,queue,time_limit,${alias}
0,70,0,pbatch,200,50
EOF

    $SIMULATOR "/tmp/alias_ar_${alias}.csv" \
        --priority_policy fcfs \
        --total_nodes 100 \
        --trace_format simple \
        --timestamp_format epoch \
        --run_time_mode actual \
        --backfill_policy easy \
        --outfile "/tmp/alias_ar_${alias}_out.csv" \
        > "/tmp/alias_ar_${alias}_err.txt" 2>&1

    if [ $? -eq 0 ]; then
        # run_time_mode=actual should read the real run time (50), not
        # time_limit (200), from whichever alias named the column.
        LINE=$(awk -F, '$1 == 0' "/tmp/alias_ar_${alias}_out.csv")
        BEGIN=$(echo "$LINE" | cut -d, -f2)
        END=$(echo "$LINE" | cut -d, -f3)
        EXEC_TIME=$((END - BEGIN))
        if [ "$EXEC_TIME" -eq 50 ]; then
            echo "✓ PASS: '$alias' recognized as actual_run_time (execution time 50s, not time_limit's 200s)"
            PASS=$((PASS + 1))
        else
            echo "✗ FAIL: '$alias' recognized but wrong execution time: ${EXEC_TIME}s (expected 50s)"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "✗ FAIL: '$alias' not recognized as actual_run_time"
        cat "/tmp/alias_ar_${alias}_err.txt"
        FAIL=$((FAIL + 1))
    fi
done

echo ""

# ------------------------------------------------------------------------
# Missing column: clear error, not a silent default
# ------------------------------------------------------------------------
echo "--- Missing time_limit column (should reject clearly) ---"

cat > /tmp/alias_no_time_limit.csv << 'EOF'
job_submit_time,num_nodes,exit_status,queue
0,70,0,pbatch
EOF

ERR_MSG=$($SIMULATOR /tmp/alias_no_time_limit.csv \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --backfill_policy easy \
    --outfile /tmp/alias_no_tl_out.csv 2>&1) || true

if echo "$ERR_MSG" | grep -q "time_limit.*not found\|not found.*time_limit"; then
    echo "✓ PASS: missing time_limit (and all aliases) rejected with a clear error"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: expected a clear 'column not found' error, got:"
    echo "$ERR_MSG"
    FAIL=$((FAIL + 1))
fi

echo ""

# Cleanup
rm -f /tmp/alias_tl_*.csv /tmp/alias_tl_*_out.csv /tmp/alias_tl_*_err.txt
rm -f /tmp/alias_ar_*.csv /tmp/alias_ar_*_out.csv /tmp/alias_ar_*_err.txt
rm -f /tmp/alias_no_time_limit.csv /tmp/alias_no_tl_out.csv

echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ "$FAIL" -eq 0 ]; then
    echo "✓ ALL COLUMN ALIAS TESTS PASSED"
    exit 0
else
    echo "✗ SOME TESTS FAILED"
    exit 1
fi
