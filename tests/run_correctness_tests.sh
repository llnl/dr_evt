#!/bin/bash
# Run complete correctness test suite
#
# Phase 1: 19 analytical tests (hand-traced oracles)
# Phase 2: 4 cross-validation tests (C++ vs Python)

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "=========================================="
echo "DR_EVT Correctness Test Suite"
echo "=========================================="
echo ""
echo "Testing 23 correctness tests:"
echo "  - 19 analytical (hand-traced oracles)"
echo "  - 4 cross-validation (C++ vs Python)"
echo ""

# Check build exists
if [ ! -f "./build/simulator" ]; then
    echo "Error: ./build/simulator not found"
    echo "Please build first: cd build && cmake .. && make"
    exit 1
fi

# Check Python oracle exists
if [ ! -f "scripts/minimal_easy_oracle.py" ]; then
    echo "Error: scripts/minimal_easy_oracle.py not found"
    exit 1
fi

# Run verification
python3 scripts/verify_against_analytical.py

exit_code=$?

if [ $exit_code -eq 0 ]; then
    echo ""
    echo "=========================================="
    echo "✓ ALL CORRECTNESS TESTS PASSED"
    echo "=========================================="
else
    echo ""
    echo "=========================================="
    echo "✗ SOME TESTS FAILED"
    echo "=========================================="
fi

exit $exit_code
