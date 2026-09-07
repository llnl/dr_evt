#!/bin/bash
# Test that configuration files produce same results as command-line options
#
# Verifies that all CLI options are supported via config files and behave identically
# NOTE: Requires Protobuf support (cmake -DDR_EVT_ENABLE_PROTOBUF=ON)
#
# This test uses --run_time_mode limit throughout (test traces lack actual_run_time column).
# Both CLI and config-file invocations use the same run_time_mode for meaningful comparison.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

# Source common simulator path finder
source "$SCRIPT_DIR/set_simulator_path.sh"

echo "=========================================="
echo "Configuration File Tests"
echo "=========================================="
echo ""
echo "Testing that config files match CLI options"
echo ""


# Check if simulator supports --config option (requires Protobuf)
if ! $SIMULATOR --help 2>&1 | grep -q -- "--config"; then
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
$SIMULATOR "$TEST_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --outfile /tmp/cli_minimal.csv

$SIMULATOR "$TEST_TRACE" \
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
$SIMULATOR "$TEST_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --backfill_policy easy \
    --priority_policy fcfs \
    --outfile /tmp/cli_full.csv

$SIMULATOR "$TEST_TRACE" \
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
$SIMULATOR "$TEST_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --backfill_policy conservative \
    --outfile /tmp/cli_conservative.csv

$SIMULATOR "$TEST_TRACE" \
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
$SIMULATOR "$TEST_TRACE" \
    --config tests/test_configs/distribution_config.pb \
    --outfile /tmp/pb_distribution.csv

if [ -f /tmp/pb_distribution.csv ]; then
    echo "  ✓ Distribution config runs"
    PASS=$((PASS + 1))
else
    echo "  ✗ Distribution config failed"
    FAIL=$((FAIL + 1))
fi

# Test 5: infile_list via protobuf config (progressive loading) vs the
# same via CLI --infile_list - no positional trace file here, unlike
# the tests above: infile_list is mutually exclusive with one.
echo "Test 5: infile_list config (progressive loading)"
$SIMULATOR \
    --infile_list tests/test_traces/progressive/file_list.txt \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --outfile /tmp/cli_infile_list.csv

$SIMULATOR \
    --config tests/test_configs/infile_list_config.pb \
    --outfile /tmp/pb_infile_list.csv

if diff -q /tmp/cli_infile_list.csv /tmp/pb_infile_list.csv > /dev/null; then
    echo "  ✓ infile_list config matches CLI"
    PASS=$((PASS + 1))
else
    echo "  ✗ infile_list config differs from CLI"
    FAIL=$((FAIL + 1))
fi

# Test 6: check_memory_pressure via protobuf config - forced
# deterministically via the DR_EVT_TEST_AVAILABLE_MEMORY_BYTES test
# seam (see get_available_memory_bytes()'s doc comment in
# src/utils/system_memory.hpp), not by relying on the test machine
# actually being low on memory.
echo "Test 6: check_memory_pressure config (forced low memory)"

if DR_EVT_TEST_AVAILABLE_MEMORY_BYTES=10 $SIMULATOR \
    --config tests/test_configs/memory_pressure_config.pb \
    --outfile /tmp/pb_memory_pressure.csv > /tmp/pb_memory_pressure.log 2>&1; then
    echo "  ✗ FAIL - expected nonzero exit under forced low memory with check_memory_pressure enabled"
    FAIL=$((FAIL + 1))
else
    if grep -q "check_memory_pressure" /tmp/pb_memory_pressure.log; then
        echo "  ✓ check_memory_pressure config correctly refused under forced low memory"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL - wrong error message"
        sed 's/^/     /' /tmp/pb_memory_pressure.log
        FAIL=$((FAIL + 1))
    fi
fi

# Test 7: every protobuf-config example in the actual documentation
# (docs/user-guide/protobuf-config.md) must itself parse and run -
# not just the hand-verified fixtures under tests/test_configs/ above,
# which can never catch a bug in the documentation's own prose
# examples (this test exists because exactly that happened: every
# example wrapped its fields in a fictional "sim_setup { ... }" block,
# and several "Run with" instructions omitted the required positional
# trace-file argument - see tests/test_protobuf_config_doc_examples.py's
# own docstring for the full story).
echo "Test 7: protobuf-config.md's own documented examples"
SIMULATOR="$SIMULATOR" python3 tests/test_protobuf_config_doc_examples.py > /tmp/doc_examples.log 2>&1
if [ $? -eq 0 ]; then
    echo "  ✓ all documented config examples parse and run correctly"
    PASS=$((PASS + 1))
else
    echo "  ✗ FAIL - one or more documented config examples are broken"
    sed 's/^/     /' /tmp/doc_examples.log
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
