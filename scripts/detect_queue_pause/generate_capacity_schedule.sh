#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./generate_capacity_schedule.sh [TRACE_PATTERN] [OUTPUT_DIRECTORY]

Generate replay evidence, inferred operating periods, two hourly capacity
schedules, and their silhouette plots. TRACE_PATTERN defaults to
*_scheduling_trace.csv and OUTPUT_DIRECTORY defaults to the current directory.

Optional environment settings:
  CAPACITY_NODES=N                  (required structural node capacity)
  TRACE_TIMEZONE=UTC
  OVERLAP_POLICY=combine
  GRACE_MINUTES=60
  RELEASE_DELAY_MINUTES=10
  CASES_PER_ROW=21
  SIMULATOR_FORMAT=0                (1 writes time,total_nodes CSVs)
  ANALYSIS_START=EPOCH_SECONDS      (inclusive; requires ANALYSIS_END)
  ANALYSIS_END=EPOCH_SECONDS        (exclusive; requires ANALYSIS_START)
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
CALL_DIR=$(pwd -P)
TRACE_PATTERN=${1:-"*_scheduling_trace.csv"}
OUTPUT_DIRECTORY=${2:-"$CALL_DIR"}

CAPACITY_NODES=${CAPACITY_NODES:-}
TRACE_TIMEZONE=${TRACE_TIMEZONE:-UTC}
OVERLAP_POLICY=${OVERLAP_POLICY:-combine}
GRACE_MINUTES=${GRACE_MINUTES:-60}
RELEASE_DELAY_MINUTES=${RELEASE_DELAY_MINUTES:-10}
CASES_PER_ROW=${CASES_PER_ROW:-21}
SIMULATOR_FORMAT=${SIMULATOR_FORMAT:-0}
ANALYSIS_START=${ANALYSIS_START:-}
ANALYSIS_END=${ANALYSIS_END:-}

if [[ -z "$CAPACITY_NODES" ]]; then
    echo "CAPACITY_NODES must be set to the machine's structural node capacity" >&2
    echo "Example: CAPACITY_NODES=158976 $0 '$TRACE_PATTERN' '$OUTPUT_DIRECTORY'" >&2
    exit 2
fi
if [[ -n "$ANALYSIS_START" || -n "$ANALYSIS_END" ]]; then
    if [[ ! "$ANALYSIS_START" =~ ^[0-9]+$ || ! "$ANALYSIS_END" =~ ^[0-9]+$ ]] ||
       (( ANALYSIS_END <= ANALYSIS_START )); then
        echo "ANALYSIS_START and ANALYSIS_END must be integer epoch seconds with END > START" >&2
        exit 2
    fi
fi
ANALYSIS_ARGS=()
if [[ -n "$ANALYSIS_START" ]]; then
    ANALYSIS_ARGS=(--analysis-start "$ANALYSIS_START" --analysis-end "$ANALYSIS_END")
fi

if [[ "$TRACE_PATTERN" != /* ]]; then
    TRACE_PATTERN="$CALL_DIR/$TRACE_PATTERN"
fi
if [[ "$OUTPUT_DIRECTORY" != /* ]]; then
    OUTPUT_DIRECTORY="$CALL_DIR/$OUTPUT_DIRECTORY"
fi
mkdir -p "$OUTPUT_DIRECTORY"
OUTPUT_DIRECTORY=$(cd "$OUTPUT_DIRECTORY" && pwd -P)

BOOTSTRAP_REPORT=$(mktemp "$OUTPUT_DIRECTORY/.capacity-bootstrap.XXXXXXXX")
cleanup() {
    rm -f "$BOOTSTRAP_REPORT"
}
trap cleanup EXIT

TIMELINE="$OUTPUT_DIRECTORY/capacity_timeline.csv"
EVIDENCE="$OUTPUT_DIRECTORY/backfill_opportunities.csv"
REPORT="$OUTPUT_DIRECTORY/maintenance_report.json"
WITH_REDUCED="$OUTPUT_DIRECTORY/resource_capacity_with_reduced_capacity.csv"
WITHOUT_REDUCED="$OUTPUT_DIRECTORY/resource_capacity_without_reduced_capacity.csv"
WITH_REDUCED_PLOT="$OUTPUT_DIRECTORY/inferred_capacity_silhouette_labeled_jst.png"
QUEUE_PAUSE_PLOT="$OUTPUT_DIRECTORY/queue_pause_capacity_silhouette_labeled_jst.png"

echo "[1/6] Building the bootstrap hourly timeline"
python3 "$SCRIPT_DIR/discover_maintenance.py" "$TRACE_PATTERN" \
    --overlap-policy "$OVERLAP_POLICY" \
    --timezone "$TRACE_TIMEZONE" \
    --known-capacity "$CAPACITY_NODES" \
    "${ANALYSIS_ARGS[@]}" \
    --all-states \
    --output "$BOOTSTRAP_REPORT" \
    --bins-output "$TIMELINE"

echo "[2/6] Replaying EASY backfill opportunities"
python3 "$SCRIPT_DIR/backfill_opportunity_audit.py" "$TRACE_PATTERN" \
    --timeline "$TIMELINE" \
    --nodes "$CAPACITY_NODES" \
    --timezone "$TRACE_TIMEZONE" \
    --overlap-policy "$OVERLAP_POLICY" \
    --grace-minutes "$GRACE_MINUTES" \
    --release-delay-minutes "$RELEASE_DELAY_MINUTES" \
    --output "$EVIDENCE"

echo "[3/6] Detecting final operating-state periods"
python3 "$SCRIPT_DIR/discover_maintenance.py" "$TRACE_PATTERN" \
    --overlap-policy "$OVERLAP_POLICY" \
    --timezone "$TRACE_TIMEZONE" \
    --known-capacity "$CAPACITY_NODES" \
    "${ANALYSIS_ARGS[@]}" \
    --backfill-evidence "$EVIDENCE" \
    --all-states \
    --output "$REPORT" \
    --bins-output "$TIMELINE"

echo "[4/6] Building capacity schedule CSV files"
BUILDER_FORMAT=()
if [[ "$SIMULATOR_FORMAT" == "1" ]]; then
    BUILDER_FORMAT=(--simulator-format)
fi
python3 "$SCRIPT_DIR/build_resource_capacity_trace.py" \
    --report "$REPORT" \
    --timeline "$TIMELINE" \
    --capacity "$CAPACITY_NODES" \
    --timezone "$TRACE_TIMEZONE" \
    --with-reduced-output "$WITH_REDUCED" \
    --without-reduced-output "$WITHOUT_REDUCED" \
    "${BUILDER_FORMAT[@]}"

CAPACITY_MPL_DIR=${MPLCONFIGDIR:-"$OUTPUT_DIRECTORY/.matplotlib-cache"}
mkdir -p "$CAPACITY_MPL_DIR"

echo "[5/6] Plotting inferred reductions and queue pauses"
MPLCONFIGDIR="$CAPACITY_MPL_DIR" python3 "$SCRIPT_DIR/plot_capacity_silhouette.py" \
    "$WITH_REDUCED" \
    --output "$WITH_REDUCED_PLOT" \
    --title "Inferred normal-queue capacity (reductions and queue pauses)" \
    --timezone "$TRACE_TIMEZONE" \
    --cases-per-row "$CASES_PER_ROW"

echo "[6/6] Plotting the queue-pause-only schedule"
MPLCONFIGDIR="$CAPACITY_MPL_DIR" python3 "$SCRIPT_DIR/plot_capacity_silhouette.py" \
    "$WITHOUT_REDUCED" \
    --output "$QUEUE_PAUSE_PLOT" \
    --title "Queue-pause-only normal-queue capacity" \
    --timezone "$TRACE_TIMEZONE" \
    --cases-per-row "$CASES_PER_ROW"

echo "Generated outputs in $OUTPUT_DIRECTORY"
printf '  %s\n' \
    "$TIMELINE" \
    "$EVIDENCE" \
    "$REPORT" \
    "$WITH_REDUCED" \
    "$WITHOUT_REDUCED" \
    "$WITH_REDUCED_PLOT" \
    "$QUEUE_PAUSE_PLOT"
