#!/bin/bash
################################################################################
# Test: EASY vs CONSERVATIVE Backfilling Correctness
#
# This test verifies the fundamental behavioral difference between
# EASY and CONSERVATIVE backfilling using a carefully crafted scenario.
#
# Scenario (100 nodes total):
# - Job 0: 60 nodes, duration 50 (starts at t=0, ends t=50)
# - Job 1: 20 nodes, duration 100 (starts at t=0, ends t=100)
# - Free nodes after Jobs 0,1 start: 20
#
# Waiting queue (FCFS order):
# - Job 2: needs 50 nodes, duration 200 → reservation at t=50
# - Job 3: needs 30 nodes, duration 50 → reservation at t=50
# - Job 4: needs 15 nodes, duration 100 → reservation at t=0 (could start now)
# - Job 5: needs 10 nodes, duration 40 → backfill candidate
#
# Expected behavior:
#
# EASY BACKFILLING:
# - Checks ONLY Job 2 (first waiting job)
# - Job 2 reservation: t=50
# - Job 5 ends at: 0 + 40 = 40
# - 40 < 50 → EASY ACCEPTS Job 5
# - Job 5 backfills and runs t=0 to t=40
# - Job 4 starts later at t=100
#
# CONSERVATIVE BACKFILLING:
# - Checks ALL waiting jobs (2, 3, 4)
# - Job 2 reservation: t=50 ✓
# - Job 3 reservation: t=50 ✓
# - Job 4 reservation: t=0 (could start immediately)
# - Job 5 ends at 40 > Job 4's reservation of 0 ❌
# - CONSERVATIVE REJECTS Job 5
# - Job 4 and Job 5 both start at t=100
#
# Key difference: Job 5 starts at t=0 (EASY) vs t=100 (CONSERVATIVE)
################################################################################

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TRACE="$SCRIPT_DIR/test_traces/feature/easy_vs_conservative_test.csv"
EXPECTED_EASY="$SCRIPT_DIR/test_traces/feature/easy_vs_conservative_expected_easy.csv"
EXPECTED_CONS="$SCRIPT_DIR/test_traces/feature/easy_vs_conservative_expected_conservative.csv"
OUTDIR="$ROOT_DIR/test_output/easy_vs_conservative"
NODES=100

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "EASY vs CONSERVATIVE Correctness Test"
echo "========================================"
echo ""
echo "Scenario: 100 nodes, 6 jobs"
echo "Testing fundamental behavioral difference"
echo ""

# Create output directory
mkdir -p "$OUTDIR"

# Verify files exist
if [ ! -f "$TRACE" ]; then
    echo -e "${RED}ERROR: Trace file not found: $TRACE${NC}"
    exit 1
fi

if [ ! -f "$EXPECTED_EASY" ]; then
    echo -e "${RED}ERROR: Expected EASY output not found: $EXPECTED_EASY${NC}"
    exit 1
fi

if [ ! -f "$EXPECTED_CONS" ]; then
    echo -e "${RED}ERROR: Expected CONSERVATIVE output not found: $EXPECTED_CONS${NC}"
    exit 1
fi

# Build if needed
if [ ! -f "$ROOT_DIR/build/simulator" ]; then
    echo "Building simulator..."
    cd "$ROOT_DIR"
    cmake -B build -S .
    cmake --build build -j
    cd -
fi

echo "=== Running EASY Backfilling ==="
"$ROOT_DIR/build/simulator" "$TRACE" \
    --total_nodes $NODES \
    --priority_policy fcfs \
    --backfill_policy easy \
    --duration_mode limit \
    --run_time_mode exact \
    --trace_format simple \
    --outfile "$OUTDIR/easy.csv" \
    > "$OUTDIR/easy.log" 2>&1

echo "=== Running CONSERVATIVE Backfilling ==="
"$ROOT_DIR/build/simulator" "$TRACE" \
    --total_nodes $NODES \
    --priority_policy fcfs_conservative \
    --backfill_policy conservative \
    --duration_mode limit \
    --run_time_mode exact \
    --trace_format simple \
    --outfile "$OUTDIR/conservative.csv" \
    > "$OUTDIR/conservative.log" 2>&1

echo ""
echo "=== Comparing Results Against Expected ==="
echo ""

# Compare using Python
python3 << 'PYTHON_SCRIPT'
import csv
import sys

def read_schedule(filename):
    """Read job schedule from CSV"""
    jobs = []
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for idx, row in enumerate(reader):
            start_time = float(row['begin_time']) if row['begin_time'] else None
            end_time = float(row['end_time']) if row['end_time'] else None
            nodes = int(row['num_nodes'])
            jobs.append({'idx': idx, 'start': start_time, 'end': end_time, 'nodes': nodes})
    return jobs

# Read schedules
easy_actual = read_schedule('test_output/easy_vs_conservative/easy.csv')
cons_actual = read_schedule('test_output/easy_vs_conservative/conservative.csv')
easy_expected = read_schedule('tests/test_traces/feature/easy_vs_conservative_expected_easy.csv')
cons_expected = read_schedule('tests/test_traces/feature/easy_vs_conservative_expected_conservative.csv')

print("EASY Backfilling Results:")
print("-" * 70)
print(f"{'Job':<6} {'Expected Start':<15} {'Actual Start':<15} {'Match':<10}")
print("-" * 70)

easy_pass = True
for i in range(len(easy_expected)):
    exp_start = easy_expected[i]['start']
    act_start = easy_actual[i]['start'] if i < len(easy_actual) else None
    match = "✓" if exp_start == act_start else "✗"
    if exp_start != act_start:
        easy_pass = False
    print(f"Job {i:<3} {exp_start:<15.1f} {act_start if act_start else 'None':<15} {match:<10}")

print("-" * 70)
print()

print("CONSERVATIVE Backfilling Results:")
print("-" * 70)
print(f"{'Job':<6} {'Expected Start':<15} {'Actual Start':<15} {'Match':<10}")
print("-" * 70)

cons_pass = True
for i in range(len(cons_expected)):
    exp_start = cons_expected[i]['start']
    act_start = cons_actual[i]['start'] if i < len(cons_actual) else None
    match = "✓" if exp_start == act_start else "✗"
    if exp_start != act_start:
        cons_pass = False
    print(f"Job {i:<3} {exp_start:<15.1f} {act_start if act_start else 'None':<15} {match:<10}")

print("-" * 70)
print()

# Critical difference: Job 5
print("Critical Behavioral Check:")
print("-" * 70)
job5_easy = easy_actual[5]['start']
job5_cons = cons_actual[5]['start']
job5_easy_exp = easy_expected[5]['start']
job5_cons_exp = cons_expected[5]['start']

print(f"Job 5 (backfill candidate, 10 nodes, duration 40):")
print(f"  EASY:         starts at t={job5_easy:.1f} (expected t={job5_easy_exp:.1f})")
print(f"  CONSERVATIVE: starts at t={job5_cons:.1f} (expected t={job5_cons_exp:.1f})")
print()

if job5_easy == 0.0 and job5_cons == 100.0:
    print("✅ CORRECT: EASY backfilled Job 5 at t=0")
    print("✅ CORRECT: CONSERVATIVE delayed Job 5 until t=100")
    print()
    print("Explanation:")
    print("  - EASY only checks Job 2's reservation (t=50)")
    print("  - Job 5 ends at t=40 < 50, so EASY accepts")
    print("  - CONSERVATIVE checks ALL jobs, including Job 4 (reservation at t=0)")
    print("  - Job 5 would end at t=40 > 0, so CONSERVATIVE rejects to protect Job 4")
    behavior_pass = True
else:
    print(f"❌ UNEXPECTED: Job 5 timing doesn't match expected behavior")
    behavior_pass = False

print("-" * 70)
print()

# Summary
if easy_pass and cons_pass and behavior_pass:
    print("✅✅✅ ALL TESTS PASSED ✅✅✅")
    print()
    print("Verified: CONSERVATIVE protects ALL waiting jobs' reservations,")
    print("          while EASY only protects the first waiting job.")
    sys.exit(0)
else:
    print("❌ SOME TESTS FAILED")
    if not easy_pass:
        print("  - EASY schedule doesn't match expected")
    if not cons_pass:
        print("  - CONSERVATIVE schedule doesn't match expected")
    if not behavior_pass:
        print("  - Key behavioral difference not observed")
    sys.exit(1)

PYTHON_SCRIPT

echo ""
echo "Test complete. Output files:"
echo "  - $OUTDIR/easy.csv"
echo "  - $OUTDIR/conservative.csv"
echo "  - $OUTDIR/easy.log"
echo "  - $OUTDIR/conservative.log"
