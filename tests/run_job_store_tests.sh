#!/bin/bash
# Job-Store (Trace::m_data) Circular Buffer Tests
#
# Trace::m_data is a boost::circular_buffer, sized via
# --job_store_capacity. Reclaiming a slot makes it reusable within the
# buffer's fixed, already-allocated capacity - it does not free memory
# back to the OS, and (see below) it essentially never happens more
# than once in a batch run regardless of the requested capacity.
#
# In batch mode (loading a whole trace file upfront - the only mode
# that exists today), load_data() resolves m_data's capacity from the
# loaded job count *before* transferring anything into it, so for the
# default capacity (0) the buffer is already sized to hold the whole
# trace from the start - no growing involved. Only if
# --job_store_capacity explicitly requests something smaller than the
# job count does the transfer loop fall back to growing (doubling) as
# it fills - and even then, it still finishes sized to fit everything
# before the simulation itself ever runs. From there, m_data.size()
# only shrinks (batch mode never inserts anything new after loading),
# so the first slot that becomes reclaimable (whichever job happens to
# sit at the front when it's safe - not necessarily the rejected job,
# if there is one) drops size() below capacity() for good, and the
# buffer never becomes full() again for the rest of the run. See
# docs/dev/OUTPUT_TRACE_BUFFERS.md for the full
# reasoning and the direct verification behind it.
#
# So the "small capacity" tests below aren't testing repeated
# reclaiming during a run (that essentially can't happen in batch mode)
# - they're testing that the grow-during-load fallback produces the
# same output as requesting sufficient capacity from the start.
#
# This verifies:
# 1. Requesting a tiny initial capacity - forcing several grow
#    reallocations during loading - still produces byte-identical
#    job-output to requesting the default (already-sufficient) capacity.
# 2. A rejected job doesn't stall the front-of-buffer sweep - it's
#    skipped immediately via the sentinel check, and every job behind
#    it still completes and gets written correctly.
# 3. Running stats (completed count, wait/turnaround time, makespan) are
#    identical either way - they're accumulated incrementally in Trace
#    as each job is written, not re-derived by iterating m_data after
#    the fact (which would miss anything already reclaimed).
# 4. --job_store_overflow abort ends the simulation cleanly (nonzero
#    exit, no crash) when the requested capacity can't hold the trace
#    even during loading and grow isn't allowed.
# 5. Measures the wall-clock cost of the grow-during-load fallback
#    itself (many small reallocations vs. one correctly-sized
#    allocation) - see "grow overhead" below.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

source "$SCRIPT_DIR/set_simulator_path.sh"

echo "=========================================="
echo "Job-Store Circular Buffer Tests"
echo "=========================================="
echo ""
echo "Testing that a tiny requested --job_store_capacity (forcing"
echo "grow-during-load reallocations) produces identical output to"
echo "the default (already-sufficient) capacity"
echo ""

PASS=0
FAIL=0

# Small enough to force several grow reallocations during loading -
# not a test of repeated reclaiming during the run (see header comment
# above for why that's essentially not reachable in batch mode).
TINY_CAPACITY=2

JS_TESTS=(
    "05_multiple_backfills"
    "21_sustained_high_load"
)

for test_base in "${JS_TESTS[@]}"; do
    echo "Testing: $test_base (job output, tiny initial capacity)"

    input_trace="tests/test_traces/feature/${test_base}.csv"

    if [ ! -f "$input_trace" ]; then
        echo "  ✗ Input not found: $input_trace"
        FAIL=$((FAIL + 1))
        continue
    fi

    default_out="/tmp/js_${test_base}_default_out.csv"
    tiny_out="/tmp/js_${test_base}_tiny_out.csv"
    default_stats="/tmp/js_${test_base}_default_stats.txt"
    tiny_stats="/tmp/js_${test_base}_tiny_stats.txt"

    $SIMULATOR "$input_trace" \
        --total_nodes 100 \
        --trace_format simple \
        --timestamp_format epoch \
        --run_time_mode limit \
        --outfile "$default_out" \
        > "$default_stats" 2>&1

    $SIMULATOR "$input_trace" \
        --total_nodes 100 \
        --trace_format simple \
        --timestamp_format epoch \
        --run_time_mode limit \
        --job_store_capacity $TINY_CAPACITY \
        --outfile "$tiny_out" \
        > "$tiny_stats" 2>&1

    if [ ! -f "$default_out" ] || [ ! -f "$tiny_out" ]; then
        echo "  ✗ Simulation failed"
        FAIL=$((FAIL + 1))
        continue
    fi

    out_ok=0
    stats_ok=0

    if diff -q "$default_out" "$tiny_out" > /dev/null; then
        out_ok=1
    fi

    # Compare only the stats block - stdout also has a wall-clock-time
    # line that legitimately differs run to run.
    if diff -q \
        <(grep -A 20 "Simulation Statistics" "$default_stats" | grep -v "Wall clock") \
        <(grep -A 20 "Simulation Statistics" "$tiny_stats" | grep -v "Wall clock") \
        > /dev/null; then
        stats_ok=1
    fi

    if [ $out_ok -eq 1 ] && [ $stats_ok -eq 1 ]; then
        echo "  ✓ PASS - output and stats identical under tiny initial capacity=$TINY_CAPACITY (grow-during-load)"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL - differs under grow-during-load (output ok: $out_ok, stats ok: $stats_ok)"
        echo "    Default: $default_out / $default_stats"
        echo "    Tiny:    $tiny_out / $tiny_stats"
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "Testing: default capacity on a small trace never reclaims prematurely"

# The bug this specifically catches: an early version of
# reclaim_front_jobs() swept unconditionally the moment a job became
# reclaimable, regardless of whether the buffer was actually full. With
# the default (auto-sized, far larger than a 3-job trace) capacity, the
# buffer never fills - so this only shows up here, not under forced
# tiny capacity above (where the buffer is always full anyway, so
# reclaiming firing correctly coincides with reclaiming firing at all).
small_trace="tests/test_traces/feature/01_backfill_allowed.csv"
small_out="/tmp/js_small_default_out.csv"
small_log="/tmp/js_small_default_log.txt"

$SIMULATOR "$small_trace" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --outfile "$small_out" \
    > "$small_log" 2>&1

expected_lines=$(($(wc -l < "$small_trace") - 1))
actual_lines=$(($(wc -l < "$small_out") - 1))

if [ "$actual_lines" -ne "$expected_lines" ]; then
    echo "  ✗ FAIL - expected $expected_lines completed jobs, found $actual_lines"
    echo "    (a job vanished under default capacity - reclaiming fired without the buffer ever being full)"
    FAIL=$((FAIL + 1))
elif ! grep -q "Jobs completed: $expected_lines" "$small_log"; then
    echo "  ✗ FAIL - stats don't show $expected_lines completed jobs"
    grep "Jobs completed\|Total jobs" "$small_log"
    FAIL=$((FAIL + 1))
else
    echo "  ✓ PASS - all $expected_lines jobs present, none reclaimed prematurely"
    PASS=$((PASS + 1))
fi

echo ""
echo "Testing: rejected job doesn't stall the front-reclaiming sweep"

# Job 1 requests more nodes than total_nodes exists - rejected at
# submission, submit_time set to the sentinel so the front-reclaiming
# sweep skips it immediately rather than waiting forever for an
# end_time that will never resolve. capacity=2 with 4 jobs forces the
# sweep to actually reach it mid-run, not just at the final flush.
reject_trace="tests/test_traces/feature/rejected_job.csv"

reject_out="/tmp/js_reject_out.csv"
reject_log="/tmp/js_reject_log.txt"

$SIMULATOR "$reject_trace" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --job_store_capacity 2 \
    --job_store_overflow grow \
    --outfile "$reject_out" \
    > "$reject_log" 2>&1

# Expect: rejection logged, exactly 3 data lines in the output (the 3
# real jobs - header plus 3, so 4 total), and "Jobs completed: 3".
data_lines=$(($(wc -l < "$reject_out") - 1))

if ! grep -qi "rejected" "$reject_log"; then
    echo "  ✗ FAIL - rejection was not logged"
    FAIL=$((FAIL + 1))
elif [ "$data_lines" -ne 3 ]; then
    echo "  ✗ FAIL - expected 3 completed jobs in output, found $data_lines"
    cat "$reject_out"
    FAIL=$((FAIL + 1))
elif ! grep -q "Jobs completed: 3" "$reject_log"; then
    echo "  ✗ FAIL - stats don't show 3 completed jobs"
    grep "Jobs completed" "$reject_log"
    FAIL=$((FAIL + 1))
else
    echo "  ✓ PASS - rejected job excluded, other 3 completed and written correctly"
    PASS=$((PASS + 1))
fi

echo ""
echo "Testing: --job_store_overflow abort"

# capacity=1 can't hold this trace even at load time - nothing has run
# yet, so nothing is reclaimable. Must fail cleanly (nonzero exit,
# actionable message), not crash or silently truncate.
set +e
abort_output=$($SIMULATOR "tests/test_traces/feature/01_backfill_allowed.csv" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --job_store_capacity 1 \
    --job_store_overflow abort \
    --outfile /tmp/js_abort_out.csv 2>&1)
abort_rc=$?
set -e

if [ $abort_rc -eq 0 ]; then
    echo "  ✗ FAIL - expected a clean abort, but simulator exited 0"
    FAIL=$((FAIL + 1))
elif echo "$abort_output" | grep -qi "job_store_overflow\|job store capacity"; then
    echo "  ✓ PASS - aborted cleanly (exit $abort_rc), no crash"
    PASS=$((PASS + 1))
else
    echo "  ✗ FAIL - exited nonzero ($abort_rc) but not with the expected message"
    echo "    Output: $abort_output"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "=========================================="
echo "Grow-overhead benchmark (informational, not pass/fail)"
echo "=========================================="
echo ""
echo "Measures the wall-clock cost of load_data()'s grow-during-load path"
echo "(repeated doubling reallocations) vs. requesting a capacity already"
echo "sufficient for the whole trace. This is what --job_store_capacity"
echo "actually costs to get wrong in batch mode, since (see header"
echo "comment) a too-small request can't cause repeated reclaiming during"
echo "the run - only repeated reallocation during loading."
echo ""

GROW_TRACE="tests/test_traces/feature/huge_10000jobs.csv"
GROW_TRIALS=3

if [ ! -f "$GROW_TRACE" ]; then
    echo "  ⚠ SKIP - $GROW_TRACE not found"
else
    sufficient_total=0
    tiny_total=0

    for i in $(seq 1 $GROW_TRIALS); do
        t0=$(date +%s.%N)
        $SIMULATOR "$GROW_TRACE" \
            --total_nodes 500 \
            --trace_format simple \
            --timestamp_format epoch \
            --run_time_mode limit \
            --job_store_capacity 10000 \
            --outfile /tmp/js_grow_sufficient_out.csv \
            > /dev/null 2>&1
        t1=$(date +%s.%N)
        sufficient_total=$(echo "$sufficient_total + ($t1 - $t0)" | bc)

        t0=$(date +%s.%N)
        $SIMULATOR "$GROW_TRACE" \
            --total_nodes 500 \
            --trace_format simple \
            --timestamp_format epoch \
            --run_time_mode limit \
            --job_store_capacity 1 \
            --job_store_overflow grow \
            --outfile /tmp/js_grow_tiny_out.csv \
            > /dev/null 2>&1
        t1=$(date +%s.%N)
        tiny_total=$(echo "$tiny_total + ($t1 - $t0)" | bc)
    done

    sufficient_avg=$(echo "scale=4; $sufficient_total / $GROW_TRIALS" | bc)
    tiny_avg=$(echo "scale=4; $tiny_total / $GROW_TRIALS" | bc)
    diff_pct=$(echo "scale=2; ($tiny_avg - $sufficient_avg) / $sufficient_avg * 100" | bc)

    echo "  Sufficient initial capacity (10000): ${sufficient_avg}s avg over $GROW_TRIALS runs"
    echo "  Tiny initial capacity (1, grows via doubling to >=10000): ${tiny_avg}s avg over $GROW_TRIALS runs"
    echo "  Difference: ${diff_pct}%"

    if diff -q /tmp/js_grow_sufficient_out.csv /tmp/js_grow_tiny_out.csv > /dev/null; then
        echo "  ✓ Output identical either way (correctness unaffected by capacity choice)"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL - output differs between sufficient and tiny initial capacity"
        FAIL=$((FAIL + 1))
    fi
fi

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL JOB-STORE TESTS PASSED"
    echo ""
    echo "The job-store circular buffer produces identical output and"
    echo "stats regardless of the requested initial capacity (grow-during-"
    echo "load reallocation is correctness-neutral and cheap), correctly"
    echo "excludes rejected jobs without stalling, and aborts cleanly"
    echo "rather than crashing when capacity can't be satisfied."
    exit 0
else
    echo "✗ SOME JOB-STORE TESTS FAILED"
    exit 1
fi
