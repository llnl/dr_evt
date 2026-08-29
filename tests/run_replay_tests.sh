#!/bin/bash
# Replay Mode Tests
#
# Verifies that replay mode faithfully reproduces simulation resource usage:
# 1. Run simulation mode → job_trace + resource_trace_sim
# 2. Replay job_trace → resource_trace_replay
# 3. Compare: resource_trace_sim == resource_trace_replay

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "=========================================="
echo "Replay Mode Tests"
echo "=========================================="
echo ""
echo "Testing that replay mode reproduces simulation resource usage"
echo ""

# Check build exists
if [ ! -f "./build/simulator" ]; then
    echo "Error: ./build/simulator not found"
    echo "Please build first: cd build && cmake .. && make"
    exit 1
fi

PASS=0
FAIL=0

# Use several correctness tests as replay test inputs
REPLAY_TESTS=(
    "bf01_basic_success_input"
    "bf04_multiple_backfill_input"
    "easy_5jobs_input"
)

for test_base in "${REPLAY_TESTS[@]}"; do
    echo "Testing: $test_base"

    input_trace="tests/test_traces/correctness/${test_base}.csv"

    if [ ! -f "$input_trace" ]; then
        echo "  ✗ Input not found: $input_trace"
        ((FAIL++))
        continue
    fi

    # Step 1: Run simulation mode
    sim_job_output="/tmp/replay_sim_${test_base}_jobs.csv"
    sim_resource_output="/tmp/replay_sim_${test_base}_resources.csv"

    ./build/simulator "$input_trace" \
        --total_nodes 100 \
        --trace_format simple \
        --timestamp_format epoch \
        --duration_mode exact \
        --outfile "$sim_job_output" \
        --resource_trace "$sim_resource_output" \
        > /dev/null 2>&1

    if [ ! -f "$sim_job_output" ] || [ ! -f "$sim_resource_output" ]; then
        echo "  ✗ Simulation failed"
        ((FAIL++))
        continue
    fi

    # Step 2: Replay the job trace
    replay_resource_output="/tmp/replay_rep_${test_base}_resources.csv"

    ./build/simulator "$sim_job_output" \
        --total_nodes 100 \
        --trace_format simple \
        --timestamp_format epoch \
        --duration_mode exact \
        --resource_trace "$replay_resource_output" \
        > /dev/null 2>&1

    if [ ! -f "$replay_resource_output" ]; then
        echo "  ✗ Replay failed"
        ((FAIL++))
        continue
    fi

    # Step 3: Compare resource traces
    if diff -q "$sim_resource_output" "$replay_resource_output" > /dev/null; then
        echo "  ✓ PASS - Resource traces match"
        ((PASS++))
    else
        echo "  ✗ FAIL - Resource traces differ"
        echo "    Simulation:  $sim_resource_output"
        echo "    Replay:      $replay_resource_output"
        ((FAIL++))
    fi
done

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL REPLAY TESTS PASSED"
    echo ""
    echo "Replay mode correctly reproduces simulation resource usage."
    exit 0
else
    echo "✗ SOME REPLAY TESTS FAILED"
    echo ""
    echo "Replay mode does not match simulation resource usage."
    exit 1
fi
