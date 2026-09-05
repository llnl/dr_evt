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
# that they produce identical schedules.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."
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
if [ ! -f "${SIMULATOR:-./build/simulator}" ]; then
    echo -e "${RED}Error: ./build/simulator not found${NC}"
    echo "Build first with:"
    echo "  cd build && cmake .. && make -j8"
    exit 1
fi

if [ ! -f "./scripts/python_conservative_scheduler.py" ]; then
    echo -e "${RED}Error: Python conservative scheduler not found${NC}"
    exit 1
fi

# Parse arguments
TRACE="${1:-tests/test_traces/scale/huge_2000jobs.csv}"
NODES="${2:-1000}"
OUTDIR="${3:-/tmp/conservative_comparison_$$}"

mkdir -p "$OUTDIR"

echo "Configuration:"
echo "  Trace:        $TRACE"
echo "  Total nodes:  $NODES"
echo "  Output dir:   $OUTDIR"
echo ""

if [ ! -f "$TRACE" ]; then
    echo -e "${RED}Error: Trace file not found: $TRACE${NC}"
    exit 1
fi

BASENAME=$(basename "$TRACE" .csv)

# Run Python reference
echo -e "${YELLOW}Running Python CONSERVATIVE reference...${NC}"
START_PY=$(date +%s)
python3 scripts/python_conservative_scheduler.py "$TRACE" \
    --nodes "$NODES" \
    --outdir "$OUTDIR" \
    > "$OUTDIR/python.log" 2>&1

END_PY=$(date +%s)
PY_TIME=$((END_PY - START_PY))

PYTHON_OUT="$OUTDIR/${BASENAME}_conservative_reference.csv"
PYTHON_RES="$OUTDIR/${BASENAME}_conservative_reference_resources.csv"

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
./build/simulator "$TRACE" \
    --total_nodes "$NODES" \
    --priority_policy fcfs_conservative \
    --backfill_policy conservative \
    --run_time_mode limit \
    --max_jobs 999999 \
    --trace_format simple \
    --outfile "$OUTDIR/cpp_conservative.csv" \
    > "$OUTDIR/cpp.log" 2>&1

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
if [ $PY_TIME -gt 0 ]; then
    SPEEDUP=$(echo "scale=1; $PY_TIME / $CPP_TIME" | bc)
    echo -e "${BLUE}Performance:${NC}"
    echo "  Python: ${PY_TIME}s"
    echo "  C++:    ${CPP_TIME}s"
    echo -e "  Speedup: ${GREEN}${SPEEDUP}x${NC}"
    echo ""
fi

# Compare schedules
echo -e "${YELLOW}Comparing schedules...${NC}"

python3 - "$PYTHON_OUT" "$CPP_OUT" << 'PYTHON_COMPARE'
import sys
import csv

def load_schedule(filename):
    """Load schedule and return sorted list of (job_id, start_time, end_time)"""
    jobs = []
    try:
        with open(filename, 'r') as f:
            reader = csv.DictReader(f)
            for idx, row in enumerate(reader):
                # Handle various column name formats
                job_id_str = row.get('job_idx', row.get('job_id', row.get('idx', '')))
                if job_id_str and job_id_str != '':
                    job_id = int(job_id_str)
                else:
                    # No job ID column - use row index
                    job_id = idx

                start_str = row.get('start_time', row.get('begin_time', ''))
                start_time = float(start_str) if start_str and start_str != '' else None

                end_str = row.get('end_time', '')
                end_time = float(end_str) if end_str and end_str != '' else None

                if start_time is not None and end_time is not None:
                    jobs.append((job_id, start_time, end_time))
    except Exception as e:
        print(f"Error loading {filename}: {e}")
        return None

    return sorted(jobs)

try:
    python_file = sys.argv[1]
    cpp_file = sys.argv[2]

    python_jobs = load_schedule(python_file)
    cpp_jobs = load_schedule(cpp_file)

    if python_jobs is None or cpp_jobs is None:
        sys.exit(1)

    if len(python_jobs) != len(cpp_jobs):
        print(f"ERROR: Job count mismatch - Python: {len(python_jobs)}, C++: {len(cpp_jobs)}")
        sys.exit(1)

    mismatches = []
    for i, (p_job, c_job) in enumerate(zip(python_jobs, cpp_jobs)):
        p_id, p_start, p_end = p_job
        c_id, c_start, c_end = c_job

        if p_id != c_id:
            mismatches.append(f"Job {i}: ID mismatch - Python: {p_id}, C++: {c_id}")
        elif abs(p_start - c_start) > 0.001 or abs(p_end - c_end) > 0.001:
            mismatches.append(f"Job {p_id}: Time mismatch")
            mismatches.append(f"  Python: start={p_start:.3f}, end={p_end:.3f}")
            mismatches.append(f"  C++:    start={c_start:.3f}, end={c_end:.3f}")
            mismatches.append(f"  Diff:   start={c_start-p_start:+.3f}, end={c_end-p_end:+.3f}")

    if mismatches:
        print("MISMATCHES FOUND:")
        for m in mismatches[:20]:  # Show first 20
            print(f"  {m}")
        if len(mismatches) > 20:
            print(f"  ... and {len(mismatches)-20} more")
        sys.exit(1)
    else:
        print(f"✓ MATCH: {len(python_jobs)} jobs, all schedules IDENTICAL")
        sys.exit(0)

except Exception as e:
    print(f"ERROR: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)
PYTHON_COMPARE

COMPARE_RESULT=$?

echo ""
if [ $COMPARE_RESULT -eq 0 ]; then
    echo -e "${GREEN}======================================================================${NC}"
    echo -e "${GREEN}✅ VERIFICATION PASSED${NC}"
    echo -e "${GREEN}======================================================================${NC}"
    echo ""
    echo "C++ and Python conservative implementations produce IDENTICAL schedules"
    echo ""
    echo "Summary:"
    echo "  Jobs compared: $PYTHON_JOBS"
    echo "  Mismatches:    0"
    echo "  Python time:   ${PY_TIME}s"
    echo "  C++ time:      ${CPP_TIME}s"
    if [ $PY_TIME -gt 0 ]; then
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
