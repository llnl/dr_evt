#!/bin/bash
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

# Compare C++ vs Python Conservative Backfilling Implementations
#
# This script runs both implementations on the same trace and verifies
# that they produce equivalent schedules within the configured time tolerance.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."
source "$SCRIPT_DIR/test_reporting.sh"
test_report_enable
cd "$REPO_ROOT"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}======================================================================${NC}"
echo -e "${BLUE}C++ vs Python CONSERVATIVE Backfilling Comparison${NC}"
echo -e "${BLUE}======================================================================${NC}"
echo ""

# Check prerequisites
source "$SCRIPT_DIR/set_simulator_path.sh"
source "$SCRIPT_DIR/select_python.sh"

if [ ! -f "./scripts/python_hybrid_scheduler.py" ]; then
    echo -e "${RED}Error: Python conservative scheduler not found${NC}"
    exit 1
fi

# Select by the actual interpreter version: python can be newer than python3
# on LC systems. Python 3.7 supplies the dataclasses module used here.
if ! select_python_interpreter 3 7; then
    echo -e "${RED}Error: no Python 3.7+ interpreter was found${NC}"
    exit 1
fi

# Parse arguments
# Keep the default large enough to exercise sustained queueing and backfilling
# while remaining practical for routine validation. Callers can pass the
# 2,000-job scaling fixture explicitly for a longer stress comparison.
TRACE="${1:-tests/test_traces/scale/xlarge_500jobs.csv}"
NODES="${2:-400}"
OUTDIR="${3:-}"
# Optional hybrid reservation depth; 0 compares full conservative
# reservations, N > 0 compares --backfill_policy hybrid against the oracle's
# --reservation-depth.
DEPTH="${4:-0}"
PY_DEPTH_ARGS=()
CPP_BACKFILL_ARGS=(--backfill_policy conservative)
if [ "$DEPTH" != "0" ]; then
    PY_DEPTH_ARGS=(--reservation-depth "$DEPTH")
    CPP_BACKFILL_ARGS=(--backfill_policy hybrid --reservation_depth "$DEPTH")
fi
if [ -z "$OUTDIR" ]; then
    OUTDIR=$(mktemp -d "/tmp/dr-evt-conservative-comparison.XXXXXXXX")
fi

mkdir -p "$OUTDIR"

echo "Configuration:"
echo "  Trace:        $TRACE"
echo "  Total nodes:  $NODES"
echo "  Output dir:   $OUTDIR"
echo "  Depth:        $DEPTH (0 = conservative, N = hybrid with N reservations)"
echo "  Python:       $PYTHON_BIN ($("$PYTHON_BIN" --version 2>&1))"
echo ""

if [ ! -f "$TRACE" ]; then
    echo -e "${RED}Error: Trace file not found: $TRACE${NC}"
    exit 1
fi

BASENAME=$(basename "$TRACE" .csv)

# Run Python reference
echo -e "${YELLOW}Running Python CONSERVATIVE reference...${NC}"
START_PY=$(date +%s)
if ! "$PYTHON_BIN" scripts/python_hybrid_scheduler.py "$TRACE" \
    --nodes "$NODES" \
    --outdir "$OUTDIR" \
    ${PY_DEPTH_ARGS[@]+"${PY_DEPTH_ARGS[@]}"} \
    > "$OUTDIR/python.log" 2>&1; then
    echo -e "${RED}✗ Python reference process failed${NC}"
    sed 's/^/  /' "$OUTDIR/python.log"
    exit 1
fi

END_PY=$(date +%s)
PY_TIME=$((END_PY - START_PY))

PYTHON_OUT="$OUTDIR/${BASENAME}_hybrid_reference.csv"
PYTHON_RES="$OUTDIR/${BASENAME}_hybrid_reference_resources.csv"

if [ ! -f "$PYTHON_OUT" ]; then
    echo -e "${RED}✗ Python reference failed${NC}"
    cat "$OUTDIR/python.log"
    exit 1
fi

echo -e "${GREEN}✓ Python completed in ${PY_TIME}s${NC}"
PYTHON_JOBS=$(tail -n +2 "$PYTHON_OUT" | wc -l | tr -d ' ')
echo "  Jobs scheduled: $PYTHON_JOBS"
echo ""

# Run C++ implementation
echo -e "${YELLOW}Running C++ CONSERVATIVE implementation...${NC}"
START_CPP=$(date +%s)
if ! "$SIMULATOR" "$TRACE" \
    --total_nodes "$NODES" \
    --priority_policy fcfs_conservative \
    "${CPP_BACKFILL_ARGS[@]}" \
    --run_time_mode limit \
    --max_jobs 999999 \
    --trace_format simple \
    --outfile "$OUTDIR/cpp_conservative.csv" \
    > "$OUTDIR/cpp.log" 2>&1; then
    echo -e "${RED}✗ C++ implementation process failed${NC}"
    sed 's/^/  /' "$OUTDIR/cpp.log"
    exit 1
fi

END_CPP=$(date +%s)
CPP_TIME=$((END_CPP - START_CPP))

CPP_OUT="$OUTDIR/cpp_conservative.csv"

if [ ! -f "$CPP_OUT" ]; then
    echo -e "${RED}✗ C++ implementation failed${NC}"
    cat "$OUTDIR/cpp.log"
    exit 1
fi

echo -e "${GREEN}✓ C++ completed in ${CPP_TIME}s${NC}"
CPP_JOBS=$(tail -n +2 "$CPP_OUT" | wc -l | tr -d ' ')
echo "  Jobs scheduled: $CPP_JOBS"
echo ""

# Performance comparison
if [ "$PY_TIME" -gt 0 ] && [ "$CPP_TIME" -gt 0 ]; then
    SPEEDUP=$(echo "scale=1; $PY_TIME / $CPP_TIME" | bc)
    echo -e "${BLUE}Performance:${NC}"
    echo "  Python: ${PY_TIME}s"
    echo "  C++:    ${CPP_TIME}s"
    echo -e "  Speedup: ${GREEN}${SPEEDUP}x${NC}"
    echo ""
fi

# Compare schedules with the same schema-normalizing comparator used by the
# EASY benchmark.
echo -e "${YELLOW}Comparing schedules...${NC}"

if "$PYTHON_BIN" tests/compare_scheduler_outputs.py \
    "$PYTHON_OUT" "$CPP_OUT" --tolerance 0.001
then
    COMPARE_RESULT=0
else
    COMPARE_RESULT=$?
fi

echo ""
if [ $COMPARE_RESULT -eq 0 ]; then
    echo -e "${GREEN}======================================================================${NC}"
    echo -e "${GREEN}✅ VERIFICATION PASSED${NC}"
    echo -e "${GREEN}======================================================================${NC}"
    echo ""
    echo "C++ and Python conservative schedules agree within 0.001"
    echo ""
    echo "Summary:"
    echo "  Jobs compared: $PYTHON_JOBS"
    echo "  Mismatches:    0"
    echo "  Python time:   ${PY_TIME}s"
    echo "  C++ time:      ${CPP_TIME}s"
    if [ "$PY_TIME" -gt 0 ] && [ "$CPP_TIME" -gt 0 ]; then
        echo "  Speedup:       ${SPEEDUP}x"
    fi
    echo ""
    echo "Output files:"
    echo "  Python: $PYTHON_OUT"
    echo "  C++:    $CPP_OUT"
    exit 0
else
    echo -e "${RED}======================================================================${NC}"
    echo -e "${RED}❌ VERIFICATION FAILED${NC}"
    echo -e "${RED}======================================================================${NC}"
    echo ""
    echo "C++ and Python implementations produce DIFFERENT schedules"
    echo ""
    echo "Investigation needed:"
    echo "  1. Check logs: $OUTDIR/python.log and $OUTDIR/cpp.log"
    echo "  2. Compare outputs: $PYTHON_OUT and $CPP_OUT"
    echo "  3. Verify algorithm implementation in both"
    exit 1
fi
