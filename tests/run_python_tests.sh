#!/bin/bash
# Python API Test Runner
#
# Tests Python bindings for DR_EVT streaming API.
# Requires building with -DDR_EVT_BUILD_PYTHON=ON

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "=========================================="
echo "Python API Tests"
echo "=========================================="
echo ""

# Check if Python bindings were built
if [ ! -d "build" ]; then
    echo "✗ Error: build/ directory not found"
    echo "Build first: cd build && cmake .. && make"
    exit 1
fi

# Check if Python module exists
PYTHON_MODULE=$(find build -name "dr_evt*.so" 2>/dev/null | head -1)
if [ -z "$PYTHON_MODULE" ]; then
    echo "✗ Error: Python module not found in build/"
    echo ""
    echo "Build Python bindings with:"
    echo "  cd build"
    echo "  cmake .. -DDR_EVT_BUILD_PYTHON=ON"
    echo "  make"
    exit 1
fi

echo "Found Python module: $PYTHON_MODULE"
echo ""

# Check Python version
PYTHON_VERSION=$(python3 --version 2>&1)
echo "Python version: $PYTHON_VERSION"
echo ""

# Run Python API tests
echo "Running Python API test suite..."
echo ""

python3 tests/test_python_api.py

EXIT_CODE=$?

echo ""
echo "=========================================="
if [ $EXIT_CODE -eq 0 ]; then
    echo "✅ PYTHON API TESTS PASSED"
else
    echo "❌ PYTHON API TESTS FAILED"
fi
echo "=========================================="

exit $EXIT_CODE
