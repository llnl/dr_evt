#!/bin/bash
# Run feature tests
#
# These tests verify specific features/modes/policies:
# - Conservative vs EASY backfilling
# - Priority policy comparisons
# - Replay vs simulation modes
# - Protobuf configuration equivalence

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "=========================================="
echo "Feature Tests"
echo "=========================================="
echo ""

# Check build exists
if [ ! -f "./build/simulator" ]; then
    echo "Error: ./build/simulator not found"
    echo "Please build first: cd build && cmake .. && make"
    exit 1
fi

PASS=0
FAIL=0

echo "=== Trace-based feature tests ==="
# Run each feature test trace (only CSV files - .trace files are documentation)
for test_file in tests/test_traces/feature/*.csv; do
    if [ ! -f "$test_file" ]; then
        continue
    fi

    test_name=$(basename "$test_file")
    echo "Testing: $test_name"

    format="simple"

    if ./build/simulator "$test_file" \
        --total_nodes 100 \
        --trace_format "$format" \
        --timestamp_format epoch \
        --duration_mode exact \
        --outfile /tmp/feature_$test_name.csv > /dev/null 2>&1; then
        echo "  ✓ PASS"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL"
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "=== Configuration tests ==="
# Run config tests (may be skipped if Protobuf disabled)
config_output=$(./tests/run_configs_tests.sh 2>&1)
config_exit=$?

if [ $config_exit -eq 0 ]; then
    if echo "$config_output" | grep -q "Skipping config tests"; then
        echo "  ⊘ Config tests SKIPPED (Protobuf disabled)"
        # Don't count skipped tests in pass/fail
    else
        echo "  ✓ Config tests PASS"
        PASS=$((PASS + 4))
    fi
else
    echo "  ✗ Config tests FAIL"
    FAIL=$((FAIL + 4))
fi

echo ""
echo "=== C++ feature tests ==="
# Check if C++ test binaries exist
for cpp_test in tests/test_streaming_api.cpp tests/test_streaming_vs_batch.cpp tests/test_two_stream_manual.cpp; do
    test_name=$(basename "$cpp_test" .cpp)
    if [ -f "./build/$test_name" ]; then
        echo "Testing: $test_name"
        if ./build/$test_name > /dev/null 2>&1; then
            echo "  ✓ PASS"
            PASS=$((PASS + 1))
        else
            echo "  ✗ FAIL"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "Skipping: $test_name (not built)"
    fi
done

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL FEATURE TESTS PASSED"
    exit 0
else
    echo "✗ SOME FEATURE TESTS FAILED"
    exit 1
fi
