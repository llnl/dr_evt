#!/bin/bash
# Verify that protected reservations are resource-time constraints, not barriers.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."
source "$SCRIPT_DIR/set_simulator_path.sh"
source "$SCRIPT_DIR/select_python.sh"
select_python_interpreter 3 7

TRACE="$SCRIPT_DIR/test_traces/feature/flexible_reservation.csv"
EXPECTED="$SCRIPT_DIR/test_traces/feature/flexible_reservation.expected.csv"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/dr-evt-flexible-reservation.XXXXXXXX")
trap 'rm -rf -- "$WORK"' EXIT

check_schedule() {
    local label=$1 output=$2 expected=$3
    "$PYTHON_BIN" "$SCRIPT_DIR/compare_scheduler_outputs.py" "$expected" "$output" \
        --tolerance 0.001 || { echo "$label did not preserve the expected reservations" >&2; exit 1; }
}

run_case() {
    local label=$1; shift
    local output="$WORK/$label.csv"
    "$SIMULATOR" "$TRACE" --total_nodes 100 --trace_format simple \
        --run_time_mode limit --outfile "$output" "$@"
    check_schedule "$label" "$output" "$EXPECTED"
}

run_case easy --priority_policy fcfs --backfill_policy easy
run_case hybrid1 --priority_policy fcfs_conservative --backfill_policy hybrid --reservation_depth 1
run_case hybrid2 --priority_policy fcfs_conservative --backfill_policy hybrid --reservation_depth 2
run_case conservative --priority_policy fcfs_conservative --backfill_policy conservative

# Point this at a separately configured -DDR_EVT_LEGACY_RESERVATION_BARRIER=ON
# binary to verify compatibility without making the normal test suite build twice.
if [ -n "${DR_EVT_LEGACY_SIMULATOR:-}" ]; then
    "$DR_EVT_LEGACY_SIMULATOR" "$TRACE" --total_nodes 100 --trace_format simple \
        --run_time_mode limit --priority_policy fcfs --backfill_policy easy \
        --outfile "$WORK/legacy.csv"
    check_schedule legacy "$WORK/legacy.csv" \
        "$SCRIPT_DIR/test_traces/feature/flexible_reservation.legacy.expected.csv"
fi

echo "Flexible reservation tests passed."
