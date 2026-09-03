#!/bin/bash
# Test that configuration files produce same results as command-line options
#
# Verifies that all CLI options are supported via config files and behave identically
# NOTE: Requires Protobuf support (cmake -DDR_EVT_ENABLE_PROTOBUF=ON)
#
# duration_mode is never set on either side of any comparison below
# (neither the CLI invocations nor the .pb configs), so it stays at its
# own default, "limit", on both sides consistently. That matters because
# duration_mode="actual" would make the scheduler ignore --run_time_mode
# entirely and just use the trace's own real run time - "limit" is what
# makes --run_time_mode exact actually get used, and keeps the
# CLI-vs-config comparisons meaningful (both sides are exercising the
# same setting, even though neither states it explicitly).

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "=========================================="
echo "Configuration File Tests"
echo "=========================================="
echo ""
echo "Testing that config files match CLI options"
echo ""

# Check build exists
if [ ! -f "./build/simulator" ]; then
    echo "Error: ./build/simulator not found"
    echo "Please build first: cd build && cmake .. && make"
    exit 1
fi

# Check if simulator supports --config option (requires Protobuf)
if ! ./build/simulator --help 2>&1 | grep -q -- "--config"; then
    echo "Simulator built without Protobuf support (no --config option)"
    echo "Skipping config tests (require -DDR_EVT_ENABLE_PROTOBUF=ON)"
    exit 0
fi

# Test trace
TEST_TRACE="tests/test_traces/unit/simple_basic.csv"

if [ ! -f "$TEST_TRACE" ]; then
    echo "Error: Test trace not found: $TEST_TRACE"
    exit 1
fi

PASS=0
FAIL=0

# Test 1: Minimal config vs CLI
echo "Test 1: Minimal config"
./build/simulator "$TEST_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode exact \
    --outfile /tmp/cli_minimal.csv

./build/simulator "$TEST_TRACE" \
    --config tests/test_configs/minimal_config.pb \
    --outfile /tmp/pb_minimal.csv

if diff -q /tmp/cli_minimal.csv /tmp/pb_minimal.csv > /dev/null; then
    echo "  ✓ Minimal config matches CLI"
    PASS=$((PASS + 1))
else
    echo "  ✗ Minimal config differs from CLI"
    FAIL=$((FAIL + 1))
fi

# Test 2: Full config vs CLI
echo "Test 2: Full config"
./build/simulator "$TEST_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode exact \
    --backfill_policy easy \
    --priority_policy fcfs \
    --outfile /tmp/cli_full.csv

./build/simulator "$TEST_TRACE" \
    --config tests/test_configs/full_config.pb \
    --outfile /tmp/pb_full.csv

if diff -q /tmp/cli_full.csv /tmp/pb_full.csv > /dev/null; then
    echo "  ✓ Full config matches CLI"
    PASS=$((PASS + 1))
else
    echo "  ✗ Full config differs from CLI"
    FAIL=$((FAIL + 1))
fi

# Test 3: Conservative config vs CLI
echo "Test 3: Conservative policy config"
./build/simulator "$TEST_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode exact \
    --backfill_policy conservative \
    --outfile /tmp/cli_conservative.csv

./build/simulator "$TEST_TRACE" \
    --config tests/test_configs/conservative_config.pb \
    --outfile /tmp/pb_conservative.csv

if diff -q /tmp/cli_conservative.csv /tmp/pb_conservative.csv > /dev/null; then
    echo "  ✓ Conservative config matches CLI"
    PASS=$((PASS + 1))
else
    echo "  ✗ Conservative config differs from CLI"
    FAIL=$((FAIL + 1))
fi

# Test 4: Distribution config
echo "Test 4: Distribution config"
./build/simulator "$TEST_TRACE" \
    --config tests/test_configs/distribution_config.pb \
    --outfile /tmp/pb_distribution.csv

if [ -f /tmp/pb_distribution.csv ]; then
    echo "  ✓ Distribution config runs"
    PASS=$((PASS + 1))
else
    echo "  ✗ Distribution config failed"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL CONFIG TESTS PASSED"
    exit 0
else
    echo "✗ SOME CONFIG TESTS FAILED"
    exit 1
fi
