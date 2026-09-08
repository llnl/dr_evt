#!/bin/bash
# Resource-History Circular Buffer Tests
#
# Trace::Context::m_resource_history is a boost::circular_buffer, bounded
# via --resource_history_capacity, with entries reclaimed (flushed to the
# resource-trace file, then discarded from memory) once it fills. This
# verifies that forcing a tiny capacity - so reclaiming fires on nearly
# every insert - still produces byte-identical resource-trace output to
# the default (auto-sized, effectively never-reclaims) capacity. Exercises
# both call sites that populate resource-history:
# 1. simulator (Simulation's scheduler-driven advance_to() loop)
# 2. tracer (Trace::run_job_trace(), fed simulator's own replay-format
#    output - the only well-formed input run_job_trace() accepts)
#
# Also verifies tracer's upfront validation: given simulation-format
# input directly (no begin_time/end_time - never valid input for
# run_job_trace(), which only replays times that are already set), it
# must reject cleanly with an actionable error, not crash.
#
# Also measures (informational, not pass/fail) the wall-clock cost of
# genuinely repeated flushing at a too-small capacity vs. a sufficient
# one - unlike job-store's m_data, resource-history has no grow option
# and no preload phase, so a too-small capacity here causes real,
# ongoing flush-and-discard for the whole run, not a one-time
# reallocation. "Sufficient" is 2x the job count here, not 1x.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

# Source common simulator and tracer path finders
source "$SCRIPT_DIR/set_simulator_path.sh"
source "$SCRIPT_DIR/set_tracer_path.sh"

echo "=========================================="
echo "Resource-History Circular Buffer Tests"
echo "=========================================="
echo ""
echo "Testing that --resource_history_capacity produces identical output"
echo "to the default capacity, even under forced extreme reclaiming"
echo ""

PASS=0
FAIL=0

# Forces reclaiming on nearly every insert - the most thorough test of the
# reclaiming/flush path's correctness.
TINY_CAPACITY=5

# Use a couple of scheduler-correctness fixtures (fast) - simulator side only.
RH_SIM_TESTS=(
    "05_multiple_backfills"
    "21_sustained_high_load"
)

for test_base in "${RH_SIM_TESTS[@]}"; do
    echo "Testing: $test_base (simulator)"

    input_trace="tests/test_traces/feature/${test_base}.csv"

    if [ ! -f "$input_trace" ]; then
        echo "  ✗ Input not found: $input_trace"
        FAIL=$((FAIL + 1))
        continue
    fi

    default_res="/tmp/rh_${test_base}_default_resources.csv"
    tiny_res="/tmp/rh_${test_base}_tiny_resources.csv"

    $SIMULATOR "$input_trace" \
        --total_nodes 100 \
        --trace_format simple \
        --timestamp_format epoch \
        --run_time_mode limit \
        --outfile "/tmp/rh_${test_base}_default_jobs.csv" \
        --resource_trace "$default_res" \
        > /dev/null 2>&1

    $SIMULATOR "$input_trace" \
        --total_nodes 100 \
        --trace_format simple \
        --timestamp_format epoch \
        --run_time_mode limit \
        --resource_history_capacity $TINY_CAPACITY \
        --outfile "/tmp/rh_${test_base}_tiny_jobs.csv" \
        --resource_trace "$tiny_res" \
        > /dev/null 2>&1

    if [ ! -f "$default_res" ] || [ ! -f "$tiny_res" ]; then
        echo "  ✗ Simulation failed"
        FAIL=$((FAIL + 1))
        continue
    fi

    if diff -q "$default_res" "$tiny_res" > /dev/null; then
        echo "  ✓ PASS - identical under forced reclaiming (capacity=$TINY_CAPACITY)"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL - resource traces differ under forced reclaiming"
        echo "    Default capacity: $default_res"
        echo "    Tiny capacity:    $tiny_res"
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "Testing: tracer (run_job_trace(), replay-format input)"

# Generate a genuine replay-format file via simulator first - tracer's
# run_job_trace() only replays begin_time/end_time that's already set,
# it never schedules anything itself (see run_job_trace()'s own upfront
# validation, tested separately below).
replay_input="/tmp/rh_tracer_replay_input.csv"
$SIMULATOR "tests/test_traces/feature/huge_2000jobs.csv" \
    --total_nodes 500 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --outfile "$replay_input" \
    --resource_trace /tmp/rh_tracer_sim_resources.csv \
    > /dev/null 2>&1

if [ ! -f "$replay_input" ]; then
    echo "  ✗ Failed to generate replay-format input"
    FAIL=$((FAIL + 1))
else
    tracer_default_res="/tmp/rh_tracer_default_resources.csv"
    tracer_tiny_res="/tmp/rh_tracer_tiny_resources.csv"

    $TRACER --infile "$replay_input" \
        --total_nodes 500 \
        --outfile /tmp/rh_tracer_default_out.csv \
        --resource_trace "$tracer_default_res" \
        --subfile /tmp/rh_tracer_default_sub.csv \
        --subsumf /tmp/rh_tracer_default_subsum.csv \
        > /dev/null 2>&1

    $TRACER --infile "$replay_input" \
        --total_nodes 500 \
        --resource_history_capacity $TINY_CAPACITY \
        --outfile /tmp/rh_tracer_tiny_out.csv \
        --resource_trace "$tracer_tiny_res" \
        --subfile /tmp/rh_tracer_tiny_sub.csv \
        --subsumf /tmp/rh_tracer_tiny_subsum.csv \
        > /dev/null 2>&1

    if [ ! -f "$tracer_default_res" ] || [ ! -f "$tracer_tiny_res" ]; then
        echo "  ✗ Tracer run failed"
        FAIL=$((FAIL + 1))
    elif diff -q "$tracer_default_res" "$tracer_tiny_res" > /dev/null; then
        echo "  ✓ PASS - identical under forced reclaiming (capacity=$TINY_CAPACITY)"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL - resource traces differ under forced reclaiming"
        echo "    Default capacity: $tracer_default_res"
        echo "    Tiny capacity:    $tracer_tiny_res"
        FAIL=$((FAIL + 1))
    fi
fi

echo ""
echo "Testing: tracer rejects simulation-format input upfront"

# huge_2000jobs.csv itself (not simulator's replay output) has no
# begin_time/end_time - the same misuse that used to reach job_stat_submit()
# and crash with std::bad_alloc before run_job_trace()'s upfront check
# was added. Must now fail cleanly (nonzero exit, no crash), not abort.
set +e
misuse_output=$($TRACER --infile "tests/test_traces/feature/huge_2000jobs.csv" \
    --total_nodes 500 \
    --outfile /tmp/rh_misuse_out.csv \
    --resource_trace /tmp/rh_misuse_resources.csv \
    --subfile /tmp/rh_misuse_sub.csv \
    --subsumf /tmp/rh_misuse_subsum.csv 2>&1)
misuse_rc=$?
set -e

if [ $misuse_rc -eq 0 ]; then
    echo "  ✗ FAIL - expected a clean rejection, but tracer exited 0"
    FAIL=$((FAIL + 1))
elif echo "$misuse_output" | grep -qi "replay-format"; then
    echo "  ✓ PASS - rejected cleanly (exit $misuse_rc), no crash"
    PASS=$((PASS + 1))
else
    echo "  ✗ FAIL - rejected (exit $misuse_rc) but not with the expected message"
    echo "    Output: $misuse_output"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "=========================================="
echo "Flush-overhead benchmark (informational, not pass/fail)"
echo "=========================================="
echo ""
echo "Unlike job-store, resource-history has no grow option - every entry"
echo "is always immediately safe to reclaim, so a too-small capacity causes"
echo "genuine, repeated flush-and-discard throughout the run, not a"
echo "one-time reallocation during loading. Sufficient capacity here is"
echo "2x the job count (each job contributes at most 2 events - start,"
echo "end), not 1x like job-store."

RH_GROW_TRACE="tests/test_traces/feature/huge_10000jobs.csv"
RH_GROW_TRIALS=3

if [ ! -f "$RH_GROW_TRACE" ]; then
    echo "  ⚠ SKIP - $RH_GROW_TRACE not found"
else
    sufficient_total=0
    tiny_total=0

    for i in $(seq 1 $RH_GROW_TRIALS); do
        t0=$(date +%s.%N)
        $SIMULATOR "$RH_GROW_TRACE" \
            --total_nodes 500 \
            --trace_format simple \
            --timestamp_format epoch \
            --run_time_mode limit \
            --resource_history_capacity 20000 \
            --outfile /tmp/rh_flush_sufficient_out.csv \
            --resource_trace /tmp/rh_flush_sufficient_resources.csv \
            > /dev/null 2>&1
        t1=$(date +%s.%N)
        sufficient_total=$(echo "$sufficient_total + ($t1 - $t0)" | bc)

        t0=$(date +%s.%N)
        $SIMULATOR "$RH_GROW_TRACE" \
            --total_nodes 500 \
            --trace_format simple \
            --timestamp_format epoch \
            --run_time_mode limit \
            --resource_history_capacity $TINY_CAPACITY \
            --outfile /tmp/rh_flush_tiny_out.csv \
            --resource_trace /tmp/rh_flush_tiny_resources.csv \
            > /dev/null 2>&1
        t1=$(date +%s.%N)
        tiny_total=$(echo "$tiny_total + ($t1 - $t0)" | bc)
    done

    sufficient_avg=$(echo "scale=4; $sufficient_total / $RH_GROW_TRIALS" | bc)
    tiny_avg=$(echo "scale=4; $tiny_total / $RH_GROW_TRIALS" | bc)
    diff_pct=$(echo "scale=2; ($tiny_avg - $sufficient_avg) / $sufficient_avg * 100" | bc)

    echo "  Sufficient capacity (2x job count = 20000): ${sufficient_avg}s avg over $RH_GROW_TRIALS runs"
    echo "  Tiny capacity ($TINY_CAPACITY, flushes repeatedly all run): ${tiny_avg}s avg over $RH_GROW_TRIALS runs"
    echo "  Difference: ${diff_pct}%"

    if diff -q /tmp/rh_flush_sufficient_resources.csv /tmp/rh_flush_tiny_resources.csv > /dev/null; then
        echo "  ✓ Resource trace identical either way (correctness unaffected by capacity choice)"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL - resource trace differs between sufficient and tiny capacity"
        FAIL=$((FAIL + 1))
    fi
fi

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL RESOURCE-HISTORY TESTS PASSED"
    echo ""
    echo "The resource-history circular buffer produces identical output"
    echo "to the default capacity, even under forced extreme reclaiming, and"
    echo "invalid input is rejected cleanly rather than crashing."
    exit 0
else
    echo "✗ SOME RESOURCE-HISTORY TESTS FAILED"
    exit 1
fi
