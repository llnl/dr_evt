#!/bin/bash
# Run unit tests - basic I/O and format tests
#
# These tests verify basic functionality:
# - Timestamp formats (epoch, ISO)
# - Simple job traces
# - Sequential execution
# - Timezone handling

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "=========================================="
echo "Unit Tests"
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

# Run each unit test
for test_file in tests/test_traces/unit/*.csv; do
    test_name=$(basename "$test_file" .csv)
    echo "Testing: $test_name"

    if ./build/simulator "$test_file" \
        --total_nodes 100 \
        --trace_format simple \
        --timestamp_format epoch \
        --duration_mode exact \
        --outfile /tmp/unit_$test_name.csv > /dev/null 2>&1; then
        echo "  ✓ PASS"
        ((PASS++))
    else
        echo "  ✗ FAIL"
        ((FAIL++))
    fi
done

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL UNIT TESTS PASSED"
    exit 0
else
    echo "✗ SOME UNIT TESTS FAILED"
    exit 1
fi
