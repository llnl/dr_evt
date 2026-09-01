#!/bin/bash
# Streaming API Test Runner
#
# Tests C++ streaming API functionality.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "=========================================="
echo "Streaming API Tests"
echo "=========================================="
echo ""

# Check if test binary exists
if [ ! -f "./build/test_streaming_api" ]; then
    echo "✗ Error: build/test_streaming_api not found"
    echo "Build first: cd build && cmake .. && make"
    exit 1
fi

echo "Running streaming API tests..."
echo ""

# Run streaming API test
./build/test_streaming_api

EXIT_CODE=$?

echo ""
echo "=========================================="
if [ $EXIT_CODE -eq 0 ]; then
    echo "✅ STREAMING API TESTS PASSED"
else
    echo "❌ STREAMING API TESTS FAILED"
fi
echo "=========================================="

exit $EXIT_CODE
