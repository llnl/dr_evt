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
# - Chains Job 2 at t=50, Job 3 at t=50, then Job 4 at t=100.
# - Job 5 ends at t=40, before the first protected reservation at t=50.
# - CONSERVATIVE therefore accepts Job 5 at t=0 without delaying the chain.
#
# This case confirms a safe backfill remains admissible under the accumulated
# resource-time plan; the dedicated chained_reservations fixture checks the
# later-reservation case directly.
################################################################################

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/test_reporting.sh"
test_report_enable
TRACE="$SCRIPT_DIR/test_traces/feature/easy_vs_conservative_test.csv"
EXPECTED_EASY="$SCRIPT_DIR/test_traces/feature/easy_vs_conservative_expected_easy.csv"
EXPECTED_CONS="$SCRIPT_DIR/test_traces/feature/easy_vs_conservative_expected_conservative.csv"
NODES=100

if ! OUTDIR="$(mktemp -d "${TMPDIR:-/tmp}/dr-evt-easy-vs-conservative.XXXXXXXX" 2>/dev/null)"; then
    OUTDIR="$(mktemp -d "/tmp/dr-evt-easy-vs-conservative.XXXXXXXX")"
fi
cleanup() { rm -rf -- "$OUTDIR"; }
test_report_set_cleanup cleanup

cd "$ROOT_DIR"

# Resolve an explicitly configured or installed simulator before falling back
# to the repository build tree.
source "$SCRIPT_DIR/set_simulator_path.sh"
source "$SCRIPT_DIR/select_python.sh"
if ! select_python_interpreter 3 6; then
    echo "ERROR: Python 3.6 or newer is required" >&2
    exit 1
fi

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

echo "=== Running EASY Backfilling ==="
"$SIMULATOR" "$TRACE" \
    --total_nodes $NODES \
    --priority_policy fcfs \
    --backfill_policy easy \
    --run_time_mode limit \
    --trace_format simple \
    --outfile "$OUTDIR/easy.csv" \
    > "$OUTDIR/easy.log" 2>&1

echo "=== Running CONSERVATIVE Backfilling ==="
"$SIMULATOR" "$TRACE" \
    --total_nodes $NODES \
    --priority_policy fcfs_conservative \
    --backfill_policy conservative \
    --run_time_mode limit \
    --trace_format simple \
    --outfile "$OUTDIR/conservative.csv" \
    > "$OUTDIR/conservative.log" 2>&1

# HYBRID with one reservation protects only the queue head, so its schedule
# must coincide with EASY on the same trace.
echo "=== Running HYBRID Backfilling (reservation depth 1) ==="
"$SIMULATOR" "$TRACE" \
    --total_nodes $NODES \
    --priority_policy fcfs_conservative \
    --backfill_policy hybrid \
    --reservation_depth 1 \
    --run_time_mode limit \
    --trace_format simple \
    --outfile "$OUTDIR/hybrid1.csv" \
    > "$OUTDIR/hybrid1.log" 2>&1

echo ""
echo "=== Comparing Results Against Expected ==="
echo ""

# Compare using Python
"$PYTHON_BIN" - "$OUTDIR/easy.csv" "$OUTDIR/conservative.csv" \
    "$EXPECTED_EASY" "$EXPECTED_CONS" << 'PYTHON_SCRIPT'
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
easy_actual = read_schedule(sys.argv[1])
cons_actual = read_schedule(sys.argv[2])
easy_expected = read_schedule(sys.argv[3])
cons_expected = read_schedule(sys.argv[4])

if len(easy_actual) != len(easy_expected):
    print(f"❌ EASY row-count mismatch: expected {len(easy_expected)}, got {len(easy_actual)}")
    sys.exit(1)
if len(cons_actual) != len(cons_expected):
    print(f"❌ CONSERVATIVE row-count mismatch: expected {len(cons_expected)}, got {len(cons_actual)}")
    sys.exit(1)

print("EASY Backfilling Results:")
print("-" * 70)
print(f"{'Job':<6} {'Expected Start':<15} {'Actual Start':<15} {'Match':<10}")
print("-" * 70)

easy_pass = True
for i, (expected, actual) in enumerate(zip(easy_expected, easy_actual)):
    exp_start = expected['start']
    act_start = actual['start']
    row_matches = expected == actual
    match = "✓" if row_matches else "✗"
    if not row_matches:
        easy_pass = False
    print(f"Job {i:<3} {exp_start:<15.1f} {act_start if act_start is not None else 'None':<15} {match:<10}")

print("-" * 70)
print()

print("CONSERVATIVE Backfilling Results:")
print("-" * 70)
print(f"{'Job':<6} {'Expected Start':<15} {'Actual Start':<15} {'Match':<10}")
print("-" * 70)

cons_pass = True
for i, (expected, actual) in enumerate(zip(cons_expected, cons_actual)):
    exp_start = expected['start']
    act_start = actual['start']
    row_matches = expected == actual
    match = "✓" if row_matches else "✗"
    if not row_matches:
        cons_pass = False
    print(f"Job {i:<3} {exp_start:<15.1f} {act_start if act_start is not None else 'None':<15} {match:<10}")

print("-" * 70)
print()

# Critical chained-plan check: Job 5 is safe before the first reservation.
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

if job5_easy == 0.0 and job5_cons == 0.0:
    print("✅ CORRECT: EASY backfilled Job 5 at t=0")
    print("✅ CORRECT: CONSERVATIVE backfilled Job 5 at t=0")
    print()
    print("Explanation:")
    print("  - EASY only checks Job 2's reservation (t=50)")
    print("  - Job 5 ends at t=40 < 50, so EASY accepts")
    print("  - CONSERVATIVE chains Job 2, Job 3, and Job 4 at 50, 50, and 100")
    print("  - Job 5 ends at t=40, before the first protected reservation")
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
        print("  - Safe chained-plan backfill was not observed")
    sys.exit(1)

PYTHON_SCRIPT

echo ""
echo "=== Checking HYBRID (depth 1) Against Expected EASY ==="
"$PYTHON_BIN" - "$OUTDIR/hybrid1.csv" "$EXPECTED_EASY" << 'PYTHON_SCRIPT'
import csv
import sys

def read_schedule(filename):
    with open(filename, 'r') as f:
        return [(float(r['begin_time']), float(r['end_time']), int(r['num_nodes']))
                for r in csv.DictReader(f)]

hybrid = read_schedule(sys.argv[1])
easy = read_schedule(sys.argv[2])
if hybrid != easy:
    print("❌ HYBRID with reservation depth 1 differs from the EASY schedule")
    for i, (h, e) in enumerate(zip(hybrid, easy)):
        if h != e:
            print(f"  Job {i}: hybrid start={h[0]} end={h[1]}, easy start={e[0]} end={e[1]}")
    sys.exit(1)
print("✅ HYBRID with reservation depth 1 reproduces the EASY schedule")
PYTHON_SCRIPT

echo ""
echo "Test complete. Temporary outputs will be removed from: $OUTDIR"
