#!/bin/bash
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

# --backfill_queue_depth under EASY backfilling (Slurm bf_max_job_test).
#
# 1. Every FCFS scheduler implementation running EASY with a queue depth
#    matches the Python EASY reference given the same depth, with and without
#    a planning horizon: deque, circular, and block queues, the multimap
#    fcfs_alt scheduler, and the fcfs_conservative ordering with EASY, which
#    selects the FCFS EASY schedulers.
# 2. A depth of 1 leaves only the head, so EASY must equal
#    --backfill_policy none for FCFS, fcfs_alt, SJF, and LJF.
# 3. Periodic EASY with a depth equals periodic hybrid with reservation
#    depth 1 and the same depth.
# 4. SJF and LJF complete the workload with a depth.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."
source "$SCRIPT_DIR/test_reporting.sh"
test_report_enable
cd "$REPO_ROOT"

# Optional first argument: the simulator binary (CTest passes its target).
if [ $# -ge 1 ]; then
    SIMULATOR="$1"
fi
source "$SCRIPT_DIR/set_simulator_path.sh"
source "$SCRIPT_DIR/select_python.sh"
if ! select_python_interpreter 3 7; then
    echo "Error: no Python 3.7+ interpreter was found"
    exit 1
fi

TRACE="tests/test_traces/scale/xlarge_500jobs.csv"
NODES=400
BASENAME=$(basename "$TRACE" .csv)
EXPECTED_JOBS=$(awk 'END {print NR - 1}' "$TRACE")

OUTDIR=$(mktemp -d "/tmp/dr-evt-backfill-queue-depth.XXXXXXXX")
cleanup() { rm -rf -- "$OUTDIR"; }
test_report_set_cleanup cleanup

FAILURES=0

echo "========================================"
echo "Backfill Queue Depth Tests (EASY)"
echo "========================================"
echo "  Trace: $TRACE ($EXPECTED_JOBS jobs), nodes: $NODES"
echo ""

run_sim() {
    # run_sim <outfile> <simulator args...>
    local out="$1"; shift
    if ! "$SIMULATOR" "$TRACE" --total_nodes "$NODES" --run_time_mode limit \
        --max_jobs 999999 --trace_format simple --outfile "$out" "$@" \
        > "$out.log" 2>&1; then
        echo "  ✗ simulator failed: $*"
        tail -5 "$out.log" | sed 's/^/    /'
        return 1
    fi
    local rows
    rows=$(tail -n +2 "$out" | wc -l | tr -d ' ')
    if [ "$rows" != "$EXPECTED_JOBS" ]; then
        echo "  ✗ $(basename "$out") has $rows rows, expected $EXPECTED_JOBS"
        return 1
    fi
}

check_match() {
    # check_match <label> <reference> <candidate>
    if "$PYTHON_BIN" tests/compare_scheduler_outputs.py "$2" "$3" \
        --tolerance 0.001 > "$3.compare" 2>&1; then
        echo "  ✓ $1"
    else
        echo "  ✗ $1"
        tail -5 "$3.compare" | sed 's/^/    /'
        FAILURES=$((FAILURES + 1))
    fi
}

# label | simulator arguments (EASY added by the loop)
IMPLEMENTATIONS=(
    "fcfs deque|--priority_policy fcfs --queue_impl deque"
    "fcfs circular|--priority_policy fcfs --queue_impl circular"
    "fcfs block|--priority_policy fcfs --queue_impl block"
    "fcfs_alt|--priority_policy fcfs_alt"
    "fcfs_conservative ordering with easy, circular|--priority_policy fcfs_conservative --queue_impl circular"
    "fcfs_conservative ordering with easy, deque|--priority_policy fcfs_conservative --queue_impl deque"
)

for SETTING in "2|" "8|" "8|2000"; do
    DEPTH="${SETTING%%|*}"
    HORIZON="${SETTING##*|}"
    REF_ARGS=(--backfill-queue-depth "$DEPTH")
    SIM_ARGS=(--backfill_queue_depth "$DEPTH")
    LABEL="depth $DEPTH"
    if [ -n "$HORIZON" ]; then
        REF_ARGS+=(--planning-horizon "$HORIZON")
        SIM_ARGS+=(--planning_horizon "$HORIZON")
        LABEL="$LABEL, horizon $HORIZON s"
    fi
    echo "=== EASY vs Python reference, $LABEL ==="
    REF_DIR="$OUTDIR/ref_${DEPTH}_${HORIZON:-inf}"
    mkdir -p "$REF_DIR"
    "$PYTHON_BIN" scripts/python_reference_scheduler.py "$TRACE" \
        --nodes "$NODES" --outdir "$REF_DIR" "${REF_ARGS[@]}" \
        > "$REF_DIR/ref.log" 2>&1
    REF="$REF_DIR/${BASENAME}_reference.csv"
    i=0
    for IMPL in "${IMPLEMENTATIONS[@]}"; do
        i=$((i + 1))
        NAME="${IMPL%%|*}"
        read -r -a ARGS <<< "${IMPL##*|}"
        OUT="$REF_DIR/sim_$i.csv"
        if run_sim "$OUT" "${ARGS[@]}" --backfill_policy easy "${SIM_ARGS[@]}"; then
            check_match "$NAME matches the reference" "$REF" "$OUT"
        else
            FAILURES=$((FAILURES + 1))
        fi
    done
    echo ""
done

echo "=== Depth 1 leaves only the head: EASY equals no backfilling ==="
for PRIORITY in fcfs fcfs_alt sjf ljf; do
    if run_sim "$OUTDIR/none_$PRIORITY.csv" --priority_policy "$PRIORITY" \
            --backfill_policy none \
        && run_sim "$OUTDIR/depth1_$PRIORITY.csv" --priority_policy "$PRIORITY" \
            --backfill_policy easy --backfill_queue_depth 1; then
        check_match "$PRIORITY easy with depth 1 equals none" \
            "$OUTDIR/none_$PRIORITY.csv" "$OUTDIR/depth1_$PRIORITY.csv"
    else
        FAILURES=$((FAILURES + 1))
    fi
done
echo ""

echo "=== Periodic EASY equals periodic hybrid depth 1 with the same queue depth ==="
if run_sim "$OUTDIR/periodic_easy.csv" --queue_impl circular \
        --backfill_policy easy --backfill_interval 30 --backfill_queue_depth 4 \
    && run_sim "$OUTDIR/periodic_hybrid.csv" --queue_impl circular \
        --backfill_policy hybrid --reservation_depth 1 --backfill_interval 30 \
        --backfill_queue_depth 4; then
    check_match "periodic easy equals periodic hybrid depth 1" \
        "$OUTDIR/periodic_hybrid.csv" "$OUTDIR/periodic_easy.csv"
else
    FAILURES=$((FAILURES + 1))
fi
echo ""

echo "=== SJF and LJF complete the workload with depth 8 ==="
for PRIORITY in sjf ljf; do
    if run_sim "$OUTDIR/$PRIORITY.csv" --priority_policy "$PRIORITY" \
            --backfill_policy easy --backfill_queue_depth 8; then
        echo "  ✓ $PRIORITY scheduled $EXPECTED_JOBS jobs"
    else
        FAILURES=$((FAILURES + 1))
    fi
done
echo ""

if [ "$FAILURES" -eq 0 ]; then
    echo "✅ ALL BACKFILL QUEUE DEPTH CHECKS PASSED"
    exit 0
fi
echo "❌ $FAILURES BACKFILL QUEUE DEPTH CHECK(S) FAILED"
exit 1
