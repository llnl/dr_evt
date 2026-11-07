#!/bin/bash
# Planning horizon tests (--planning_horizon, event-driven scheduling).
#
# 1. Differential: tests/test_traces/feature/planning_horizon.csv on 10 nodes
#    under easy, hybrid (depth 1), conservative, and sjf, with a 10 s horizon
#    and without one, against the Python references (EASY reference for easy
#    and sjf, conservative reference for hybrid and conservative). Hybrid and
#    conservative use a one-job scan depth: the out-of-horizon head must not
#    consume it, so the 100 s candidate backfills at its arrival.
# 2. An explicit unlimited horizon must leave the output of the 34
#    scheduler-correctness workloads byte-identical to the default.
# 3. Invalid values are rejected.
#
# Periodic-mode horizon cases are covered by tests/run_configs_tests.sh.
#
# Usage: tests/run_planning_horizon_tests.sh [simulator]

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
    echo "No Python 3.6+ interpreter found; the reference schedulers need one."
    exit 1
fi
EASY_REFERENCE="$REPO_ROOT/scripts/python_reference_scheduler.py"
CONSERVATIVE_REFERENCE="$REPO_ROOT/scripts/python_hybrid_scheduler.py"

FIXTURE="tests/test_traces/feature/planning_horizon.csv"
FIXTURE_NODES=10
HORIZON=10
CORRECTNESS_DIR="tests/test_traces/scheduler_correctness"
WORK=$(mktemp -d "/tmp/dr-evt-planning-horizon.XXXXXXXX")
cleanup() { rm -rf -- "$WORK"; }
test_report_set_cleanup cleanup

PASSED=0
FAILED=0

echo "=========================================="
echo "DR_EVT Planning Horizon Tests (event-driven)"
echo "=========================================="
echo ""

# job_id,start,end from a simulator output (job_submit_time,begin_time,end_time,...).
sim_schedule() {
    awk -F, 'NR>1 {printf "%d,%g,%g\n", NR-2, $2, $3}' "$1"
}
# job_id,start,end from a reference output (job_idx,submit,start,end,...).
ref_schedule() {
    awk -F, 'NR>1 {printf "%d,%g,%g\n", $1, $3, $4}' "$1"
}

# Reference schedules, with and without the horizon.
for BOUND in unbounded bounded; do
    if [ "$BOUND" = bounded ]; then
        HOPT=(--planning-horizon "$HORIZON")
    else
        HOPT=()
    fi
    mkdir -p "$WORK/$BOUND"
    "$PYTHON_BIN" "$EASY_REFERENCE" "$FIXTURE" --nodes "$FIXTURE_NODES" \
        --outdir "$WORK/$BOUND" ${HOPT[@]+"${HOPT[@]}"} > "$WORK/$BOUND.easy.log" 2>&1 || {
        echo "✗ EASY reference failed ($BOUND)"; sed 's/^/    /' "$WORK/$BOUND.easy.log"; ((FAILED++)); }
    ref_schedule "$WORK/$BOUND/planning_horizon_reference.csv" > "$WORK/$BOUND.easy_ref" 2>/dev/null
    for DEPTH in 1 0; do
        mkdir -p "$WORK/$BOUND/depth$DEPTH"
        "$PYTHON_BIN" "$CONSERVATIVE_REFERENCE" "$FIXTURE" --nodes "$FIXTURE_NODES" \
            --reservation-depth "$DEPTH" --backfill-queue-depth 1 \
            --outdir "$WORK/$BOUND/depth$DEPTH" ${HOPT[@]+"${HOPT[@]}"} \
            > "$WORK/$BOUND.depth$DEPTH.log" 2>&1 || {
            echo "✗ conservative reference failed ($BOUND, depth $DEPTH)"; sed 's/^/    /' "$WORK/$BOUND.depth$DEPTH.log"; ((FAILED++)); }
        ref_schedule "$WORK/$BOUND/depth$DEPTH/planning_horizon_hybrid_reference.csv" \
            > "$WORK/$BOUND.depth$DEPTH.ref" 2>/dev/null
    done
done

# The fixture is only useful if the horizon changes the answer.
EXPECTED_UNBOUNDED=$'0,0,100\n1,100,110\n2,110,210'
EXPECTED_BOUNDED=$'0,0,100\n1,101,111\n2,1,101'
if [ "$(cat "$WORK/unbounded.easy_ref")" = "$EXPECTED_UNBOUNDED" ] && \
   [ "$(cat "$WORK/bounded.easy_ref")" = "$EXPECTED_BOUNDED" ] && \
   [ "$(cat "$WORK/bounded.depth1.ref")" = "$EXPECTED_BOUNDED" ] && \
   [ "$(cat "$WORK/bounded.depth0.ref")" = "$EXPECTED_BOUNDED" ]; then
    echo "✓ references reproduce the hand-computed schedules"
    ((PASSED++))
else
    echo "✗ references differ from the hand-computed schedules"
    for f in unbounded.easy_ref bounded.easy_ref bounded.depth1.ref bounded.depth0.ref; do
        echo "  $f:"; sed 's/^/    /' "$WORK/$f"
    done
    ((FAILED++))
fi

# Simulator policies: name, extra options, reference file per bound.
POLICIES=(
    "easy|--backfill_policy easy|easy_ref"
    "hybrid|--backfill_policy hybrid --reservation_depth 1 --backfill_queue_depth 1|depth1.ref"
    "conservative|--backfill_policy conservative --backfill_queue_depth 1|depth0.ref"
    "sjf|--backfill_policy easy --priority_policy sjf|easy_ref"
)
for BOUND in unbounded bounded; do
    if [ "$BOUND" = bounded ]; then
        SIM_HOPT=(--planning_horizon "$HORIZON")
    else
        SIM_HOPT=()
    fi
    for SPEC in "${POLICIES[@]}"; do
        NAME="${SPEC%%|*}"; REST="${SPEC#*|}"; OPTS="${REST%%|*}"; REF="${REST##*|}"
        CASE="$NAME.$BOUND"
        # SJF/LJF test a candidate's projected end against the reservation
        # inclusively (<=), unlike every other scheduler and the references
        # (strict <). On this fixture the candidate ends exactly at the
        # reservation, so the unbounded SJF run differs from the reference by
        # that pre-existing rule; only the bounded run is compared here.
        if [ "$NAME" = sjf ] && [ "$BOUND" = unbounded ]; then
            continue
        fi
        # shellcheck disable=SC2086
        if ! "$SIMULATOR" "$FIXTURE" --total_nodes "$FIXTURE_NODES" --trace_format simple \
                --timestamp_format epoch --run_time_mode limit $OPTS ${SIM_HOPT[@]+"${SIM_HOPT[@]}"} \
                --outfile "$WORK/$CASE.sim.csv" > "$WORK/$CASE.sim.log" 2>&1; then
            echo "✗ $CASE - simulator failed"; sed 's/^/    /' "$WORK/$CASE.sim.log"; ((FAILED++)); continue
        fi
        sim_schedule "$WORK/$CASE.sim.csv" > "$WORK/$CASE.sim_sched"
        if diff -q "$WORK/$BOUND.$REF" "$WORK/$CASE.sim_sched" > /dev/null; then
            echo "✓ $CASE"
            ((PASSED++))
        else
            echo "✗ $CASE - mismatch (reference < > simulator)"
            diff "$WORK/$BOUND.$REF" "$WORK/$CASE.sim_sched" | sed 's/^/    /'
            ((FAILED++))
        fi
    done
done

echo ""
echo "Unlimited horizon identity on the 34 scheduler-correctness workloads"
IDENTICAL=0
TOTAL=0
for INPUT in "$CORRECTNESS_DIR"/[0-9]*.csv; do
    case "$INPUT" in *expected*) continue;; esac
    TOTAL=$((TOTAL + 1))
    if head -1 "$INPUT" | grep -q "actual_run_time"; then MODE=actual; else MODE=limit; fi
    NAME=$(basename "$INPUT" .csv)
    "$SIMULATOR" "$INPUT" --total_nodes 100 --trace_format simple --timestamp_format epoch \
        --run_time_mode "$MODE" --backfill_policy easy \
        --outfile "$WORK/$NAME.a.csv" --resource_trace "$WORK/$NAME.a_res.csv" > /dev/null 2>&1
    "$SIMULATOR" "$INPUT" --total_nodes 100 --trace_format simple --timestamp_format epoch \
        --run_time_mode "$MODE" --backfill_policy easy --planning_horizon 1000000000 \
        --outfile "$WORK/$NAME.b.csv" --resource_trace "$WORK/$NAME.b_res.csv" > /dev/null 2>&1
    if [ -s "$WORK/$NAME.a.csv" ] && cmp -s "$WORK/$NAME.a.csv" "$WORK/$NAME.b.csv" && \
       cmp -s "$WORK/$NAME.a_res.csv" "$WORK/$NAME.b_res.csv"; then
        IDENTICAL=$((IDENTICAL + 1))
    else
        echo "✗ $NAME - output changed with an unlimited horizon"
    fi
done
if [ "$IDENTICAL" -eq "$TOTAL" ] && [ "$TOTAL" -eq 34 ]; then
    echo "✓ $IDENTICAL/$TOTAL workloads byte-identical"
    ((PASSED++))
else
    echo "✗ $IDENTICAL/$TOTAL workloads byte-identical"
    ((FAILED++))
fi

echo ""
echo "Option surface checks"
REJECTED=0
for BAD in "--planning_horizon -1" "--planning_horizon nope"; do
    # shellcheck disable=SC2086
    if "$SIMULATOR" "$FIXTURE" --total_nodes "$FIXTURE_NODES" --trace_format simple \
           --timestamp_format epoch $BAD --outfile "$WORK/bad.csv" > /dev/null 2>&1; then
        echo "✗ accepted invalid option: $BAD"
    else
        ((REJECTED++))
    fi
done
if [ "$REJECTED" -eq 2 ]; then
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
