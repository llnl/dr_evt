#!/bin/bash
# Periodic backfill tests (--backfill_interval).
#
# 1. Differential: run the 34 scheduler-correctness workloads with periodic
#    backfill on the simulator and with --backfill-interval on the Python
#    conservative reference, for hybrid (reservation depth 1, EASY-like) and
#    conservative backfill at two tick intervals, and compare job schedules and
#    settled resource traces (time, free, allocated). The intervals are not
#    multiples of the workloads' 5 s arrival cadence, so ticks fall between
#    events as well as on them, and ticks pass while nothing is waiting.
# 2. Starts that expose a new bounded queue prefix must enable the next tick
#    without resetting the current tick's examination budget.
# 3. Hybrid/conservative admission and reservation placement include endpoints.
# 4. A zero interval must leave the output byte-identical to a run without the
#    option.
# 5. Invalid values and an unsupported queue implementation are rejected.
#
# Usage: tests/run_periodic_backfill_tests.sh [simulator]
#   DR_EVT_BACKFILL_INTERVALS overrides the tick intervals (default "7 30").

set -u

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."
source "$SCRIPT_DIR/test_reporting.sh"
test_report_enable
cd "$REPO_ROOT"

if [ $# -ge 1 ]; then
    SIMULATOR="$1"
fi
source "$SCRIPT_DIR/set_simulator_path.sh"
source "$SCRIPT_DIR/select_python.sh"
if ! select_python_interpreter 3 6; then
    echo "No Python 3.6+ interpreter found; the reference scheduler needs one."
    exit 1
fi
REFERENCE="$REPO_ROOT/scripts/python_hybrid_scheduler.py"

TRACE_DIR="tests/test_traces/scheduler_correctness"
TOTAL_NODES=100
read -r -a INTERVALS <<< "${DR_EVT_BACKFILL_INTERVALS:-7 30}"
WORK=$(mktemp -d "/tmp/dr-evt-periodic-backfill.XXXXXXXX")
cleanup() { rm -rf -- "$WORK"; }
test_report_set_cleanup cleanup

TESTS=(
    "01_backfill_allowed" "02_backfill_blocked_time"
    "03_backfill_blocked_resources" "04_backfill_resource_competition"
    "05_multiple_backfills" "06_backfill_out_of_order"
    "07_simultaneous_submit" "08_simultaneous_completion"
    "09_simultaneous_submit_complete" "10_queue_drain_idle"
    "11_multiple_drains" "12_drain_with_backlog" "13_consecutive_fcfs"
    "14_fcfs_with_backfill" "15_fcfs_partial_overlap"
    "16_starvation_prevention" "17_late_large_priority"
    "18_backfill_no_starvation" "19_resource_fragmentation"
    "20_fragmentation_recovery" "21_sustained_high_load" "22_bursty_load"
    "23_mixed_load" "24_multiple_running_jobs" "25_early_completion_basic"
    "26_early_completion_cascading" "27_early_vs_late_completion"
    "28_simultaneous_completions_backfill"
    "29_large_completion_multiple_backfills" "30_fcfs_blocked_backfill_past"
    "31_completion_arrival_with_queue"
    "32_simultaneous_backfill_with_reservation" "33_five_simultaneous_events"
    "34_backfill_overallocation"
)

# Simulator backfill policy and reference reservation depth, per case.
POLICIES=("hybrid:1" "conservative:0")

PASSED=0
FAILED=0

echo "=========================================="
echo "DR_EVT Periodic Backfill Tests (intervals: ${INTERVALS[*]} s)"
echo "=========================================="
echo ""

# Keep the last row per timestamp (the settled state) and normalize numbers
# so "200" and "200.0" compare equal. Drops the simulator's all-free baseline
# row at time 0, which the reference does not emit.
settle() {
    awk -F, 'NR==1 {next}
             {t=$1+0; if (t==0 && $3+0==0) next;
              rows[t]=sprintf("%g,%g,%g", t, $2, $3);
              if (!(t in order)) order[t]=++n}
             END {for (i=1;i<=n;i++) for (t in order) if (order[t]==i) print rows[t]}' "$1"
}

for POLICY_SPEC in "${POLICIES[@]}"; do
    POLICY="${POLICY_SPEC%%:*}"
    DEPTH="${POLICY_SPEC##*:}"
    for INTERVAL in "${INTERVALS[@]}"; do
        echo "--- ${POLICY} backfill, ${INTERVAL} s ticks"
        for TEST in "${TESTS[@]}"; do
            INPUT="${TRACE_DIR}/${TEST}.csv"
            CASE="${TEST}.${POLICY}.${INTERVAL}"
            if [ ! -f "$INPUT" ]; then
                echo "✗ $CASE - input not found"
                ((FAILED++))
                continue
            fi
            if head -1 "$INPUT" | grep -q "actual_run_time"; then
                RUN_TIME_MODE="actual"
            else
                RUN_TIME_MODE="limit"
            fi

            # Reference schedule and resources with the same ticks. The
            # reference starts its ticks at the interval, the simulator at
            # the first event time plus the interval; both are multiples of
            # the interval for these workloads, which all start at t=0.
            if ! "$PYTHON_BIN" "$REFERENCE" "$INPUT" --nodes "$TOTAL_NODES" \
                    --outdir "$WORK" --reservation-depth "$DEPTH" \
                    --backfill-interval "$INTERVAL" \
                    > "$WORK/${CASE}.ref.log" 2>&1; then
                echo "✗ $CASE - reference scheduler failed"
                sed 's/^/    /' "$WORK/${CASE}.ref.log"
                ((FAILED++))
                continue
            fi
            REF_JOBS="$WORK/${TEST}_hybrid_reference.csv"
            REF_RES="$WORK/${TEST}_hybrid_reference_resources.csv"

            SIM_OUT="$WORK/${CASE}.sim.csv"
            SIM_RES="$WORK/${CASE}.sim_resources.csv"
            if ! "$SIMULATOR" "$INPUT" --total_nodes "$TOTAL_NODES" \
                    --trace_format simple --timestamp_format epoch \
                    --run_time_mode "$RUN_TIME_MODE" \
                    --backfill_policy "$POLICY" --reservation_depth "$DEPTH" \
                    --backfill_interval "$INTERVAL" \
                    --outfile "$SIM_OUT" --resource_trace "$SIM_RES" \
                    > "$WORK/${CASE}.sim.log" 2>&1; then
                echo "✗ $CASE - simulator failed"
                sed 's/^/    /' "$WORK/${CASE}.sim.log"
                ((FAILED++))
                continue
            fi

            # Schedules: job_id,start,end. Reference columns:
            # job_idx,submit_time,start_time,end_time,nodes,duration.
            # Simulator: job_submit_time,begin_time,end_time,...
            awk -F, 'NR>1 {printf "%d,%g,%g\n", $1, $3, $4}' "$REF_JOBS" > "$WORK/${CASE}.ref_sched"
            awk -F, 'NR>1 {printf "%d,%g,%g\n", NR-2, $2, $3}' "$SIM_OUT" > "$WORK/${CASE}.sim_sched"
            settle "$REF_RES" > "$WORK/${CASE}.ref_settled"
            settle "$SIM_RES" > "$WORK/${CASE}.sim_settled"

            if diff -q "$WORK/${CASE}.ref_sched" "$WORK/${CASE}.sim_sched" > /dev/null && \
               diff -q "$WORK/${CASE}.ref_settled" "$WORK/${CASE}.sim_settled" > /dev/null; then
                echo "✓ $CASE"
                ((PASSED++))
            else
                echo "✗ $CASE - mismatch"
                echo "  schedule diff (reference < > simulator):"
                diff "$WORK/${CASE}.ref_sched" "$WORK/${CASE}.sim_sched" | head -10 | sed 's/^/    /'
                echo "  settled resource diff (reference < > simulator):"
                diff "$WORK/${CASE}.ref_settled" "$WORK/${CASE}.sim_settled" | head -10 | sed 's/^/    /'
                ((FAILED++))
            fi
        done
    done
done

echo ""
echo "Bounded queue retries after backfill starts"

# The first tick starts two jobs from Q=3 and leaves one free node. A third
# filler moves into Q and must start at the next tick, before any completion.
# It must not start in a repeated pass at the first tick, which shares Q.
cat > "$WORK/retry.csv" <<'EOF'
job_submit_time,num_nodes,time_limit,actual_run_time
0,1,600,600
1,4,60,60
1,1,120,120
1,1,120,120
1,1,120,120
EOF
cat > "$WORK/retry.expected" <<'EOF'
0,0,600
1,600,660
2,30,150
3,30,150
4,60,180
EOF
for POLICY_SPEC in "hybrid:32" "conservative:0" "easy:1"; do
    POLICY="${POLICY_SPEC%%:*}"
    DEPTH="${POLICY_SPEC##*:}"
    if "$SIMULATOR" "$WORK/retry.csv" --total_nodes 4 \
            --trace_format simple --timestamp_format epoch --run_time_mode actual \
            --queue_impl circular --dispatch_interval 0 --backfill_policy "$POLICY" \
            --reservation_depth "$DEPTH" --backfill_interval 30 --backfill_queue_depth 3 \
            --outfile "$WORK/retry.$POLICY.csv" > "$WORK/retry.$POLICY.log" 2>&1; then
        awk -F, 'NR>1 {printf "%d,%g,%g\n", NR-2, $2, $3}' \
            "$WORK/retry.$POLICY.csv" > "$WORK/retry.$POLICY.actual"
        if diff -q "$WORK/retry.expected" "$WORK/retry.$POLICY.actual" > /dev/null; then
            echo "✓ $POLICY retries the newly eligible queue prefix at the next tick"
            ((PASSED++))
            continue
        fi
        diff "$WORK/retry.expected" "$WORK/retry.$POLICY.actual"
    else
        cat "$WORK/retry.$POLICY.log"
    fi
    echo "✗ $POLICY did not preserve the next tick or the per-tick queue budget"
    ((FAILED++))
done

echo ""
echo "Option surface checks"

# C ends exactly when B's reservation begins. Slurm rejects that immediate
# fit and places C's reservation after B, leaving room for D at the first tick.
cat > "$WORK/endpoint.csv" <<'EOF'
job_submit_time,num_nodes,time_limit,actual_run_time
0,2,120,120
1,4,60,60
1,2,90,90
1,1,30,30
EOF
cat > "$WORK/endpoint.expected" <<'EOF'
0,0,120
1,120,180
2,180,270
3,30,60
EOF
for POLICY_SPEC in "hybrid:32" "conservative:0"; do
    POLICY="${POLICY_SPEC%%:*}"
    DEPTH="${POLICY_SPEC##*:}"
    if "$SIMULATOR" "$WORK/endpoint.csv" --total_nodes 4 \
            --trace_format simple --timestamp_format epoch --run_time_mode actual \
            --queue_impl circular --dispatch_interval 0 --backfill_policy "$POLICY" \
            --reservation_depth "$DEPTH" --backfill_interval 30 \
            --outfile "$WORK/endpoint.$POLICY.csv" > "$WORK/endpoint.$POLICY.log" 2>&1; then
        awk -F, 'NR>1 {printf "%d,%g,%g\n", NR-2, $2, $3}' \
            "$WORK/endpoint.$POLICY.csv" > "$WORK/endpoint.$POLICY.actual"
        if diff -q "$WORK/endpoint.expected" "$WORK/endpoint.$POLICY.actual" > /dev/null; then
            echo "✓ $POLICY includes endpoints in admission and reservation placement"
            ((PASSED++))
            continue
        fi
        diff "$WORK/endpoint.expected" "$WORK/endpoint.$POLICY.actual"
    else
        cat "$WORK/endpoint.$POLICY.log"
    fi
    echo "✗ $POLICY reservation endpoint mismatch"
    ((FAILED++))
done

# Explicit zero interval: byte-identical output to a run without the option.
BASE_IN="${TRACE_DIR}/05_multiple_backfills.csv"
"$SIMULATOR" "$BASE_IN" --total_nodes "$TOTAL_NODES" --trace_format simple \
    --timestamp_format epoch --run_time_mode limit --backfill_policy hybrid \
    --reservation_depth 1 \
    --outfile "$WORK/zero_a.csv" --resource_trace "$WORK/zero_a_res.csv" > /dev/null 2>&1
"$SIMULATOR" "$BASE_IN" --total_nodes "$TOTAL_NODES" --trace_format simple \
    --timestamp_format epoch --run_time_mode limit --backfill_policy hybrid \
    --reservation_depth 1 --backfill_interval 0 \
    --outfile "$WORK/zero_b.csv" --resource_trace "$WORK/zero_b_res.csv" > /dev/null 2>&1
if [ -s "$WORK/zero_a.csv" ] && cmp -s "$WORK/zero_a.csv" "$WORK/zero_b.csv" && \
   cmp -s "$WORK/zero_a_res.csv" "$WORK/zero_b_res.csv"; then
    echo "✓ zero interval is byte-identical to no option"
    ((PASSED++))
else
    echo "✗ zero interval changed the output"
    ((FAILED++))
fi

# Invalid values, a disabled backfill policy, and the non-circular queue
# implementations are rejected.
REJECTED=0
for BAD in "--backfill_policy hybrid --backfill_interval -1" \
           "--backfill_policy hybrid --backfill_interval nope" \
           "--backfill_policy none --backfill_interval 5" \
           "--backfill_policy hybrid --queue_impl deque --backfill_interval 5"; do
    # shellcheck disable=SC2086
    if "$SIMULATOR" "$BASE_IN" --total_nodes "$TOTAL_NODES" --trace_format simple \
           --timestamp_format epoch $BAD \
           --outfile "$WORK/bad.csv" > /dev/null 2>&1; then
        echo "✗ accepted invalid option: $BAD"
    else
        ((REJECTED++))
    fi
done
if [ "$REJECTED" -eq 4 ]; then
    echo "✓ invalid values rejected"
    ((PASSED++))
else
    ((FAILED++))
fi

echo ""
echo "=========================================="
echo "Results: $PASSED passed, $FAILED failed"
echo "=========================================="
[ "$FAILED" -eq 0 ]
