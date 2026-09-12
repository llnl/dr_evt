#!/bin/bash
# Progressive Loading (--infile_list) Tests
#
# Runs both test_progressive_load.cpp (C++ level - direct Sim_Params
# construction, checks m_data.capacity() directly to confirm memory is
# actually bounded) and the CLI-level tests below it, which exercise
# the actual getopt() parsing path (Sim_Params::getopt(), the
# --infile_list list-file reading, the positional-argument validation
# change) that the C++-level test doesn't cover - see
# docs/dev/OUTPUT_TRACE_BUFFERS.md.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

source "$SCRIPT_DIR/set_simulator_path.sh"

echo "=========================================="
echo "Progressive Loading (--infile_list) Tests"
echo "=========================================="
echo ""

PASS=0
FAIL=0
TRACE_DIR="tests/test_traces/progressive"

# --- Test 0: the C++-level test binary (test_progressive_load.cpp) -
# constructs Sim_Params directly, so it covers correctness and memory
# bounding more precisely than a CLI invocation can (checking
# m_data.capacity() directly) - the CLI tests below cover what that
# can't: the actual getopt() parsing path.
echo "Testing: progressive_load_api (C++ level)"

PROGRESSIVE_API_BIN="${CMAKE_INSTALL_PREFIX:-./install}/bin/tests/test_progressive_load"

if [ ! -x "$PROGRESSIVE_API_BIN" ]; then
    echo "  ✗ FAIL - installed test_progressive_load binary not found"
    echo "    Expected: $PROGRESSIVE_API_BIN"
    FAIL=$((FAIL + 1))
else
    if "$PROGRESSIVE_API_BIN" > /tmp/progressive_load_api_out.txt 2>&1; then
        echo "  ✓ PASS"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL"
        sed 's/^/    /' /tmp/progressive_load_api_out.txt
        FAIL=$((FAIL + 1))
    fi
fi

# --- Test 1: basic --infile_list run matches the combined single-file run ---
echo "Testing: infile_list_matches_combined"

OUT_LIST="/tmp/progressive_cli_list_out.csv"
OUT_COMBINED="/tmp/progressive_cli_combined_out.csv"
rm -f "$OUT_LIST" "$OUT_COMBINED"

$SIMULATOR --infile_list "$TRACE_DIR/file_list.txt" \
    --total_nodes 100 --trace_format simple --timestamp_format epoch \
    --run_time_mode limit --outfile "$OUT_LIST" > /dev/null 2>&1 || true

$SIMULATOR "$TRACE_DIR/combined.csv" \
    --total_nodes 100 --trace_format simple --timestamp_format epoch \
    --run_time_mode limit --outfile "$OUT_COMBINED" > /dev/null 2>&1 || true

if [ -f "$OUT_LIST" ] && [ -f "$OUT_COMBINED" ] && diff -w "$OUT_LIST" "$OUT_COMBINED" > /dev/null 2>&1; then
    echo "  ✓ PASS"
    PASS=$((PASS + 1))
else
    echo "  ✗ FAIL - schedules differ or one didn't produce output"
    FAIL=$((FAIL + 1))
fi

# --- Test 2: --infile_list with a small --job_store_capacity actually
# bounds memory (only directly observable via the C++-level test's
# capacity() check - this CLI test just confirms the run itself
# succeeds end-to-end under a small capacity, not silently misbehaving) ---
echo "Testing: infile_list_with_small_job_store_capacity"

OUT_BOUNDED="/tmp/progressive_cli_bounded_out.csv"
rm -f "$OUT_BOUNDED"

$SIMULATOR --infile_list "$TRACE_DIR/file_list.txt" \
    --total_nodes 100 --trace_format simple --timestamp_format epoch \
    --run_time_mode limit --job_store_capacity 2 --job_store_overflow grow \
    --outfile "$OUT_BOUNDED" > /dev/null 2>&1 || true

if [ -f "$OUT_BOUNDED" ] && diff -w "$OUT_BOUNDED" "$OUT_COMBINED" > /dev/null 2>&1; then
    echo "  ✓ PASS"
    PASS=$((PASS + 1))
else
    echo "  ✗ FAIL"
    FAIL=$((FAIL + 1))
fi

# --- Test 3: empty --infile_list file is rejected with a clear error,
# not a crash ---
echo "Testing: infile_list_empty_file_rejected"

EMPTY_LIST="/tmp/progressive_cli_empty_list.txt"
: > "$EMPTY_LIST"

if $SIMULATOR --infile_list "$EMPTY_LIST" --total_nodes 100 > /tmp/progressive_cli_empty.log 2>&1; then
    echo "  ✗ FAIL - expected nonzero exit for an empty --infile_list file"
    FAIL=$((FAIL + 1))
else
    if grep -q "contains no file paths" /tmp/progressive_cli_empty.log; then
        echo "  ✓ PASS"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL - wrong error message"
        sed 's/^/     /' /tmp/progressive_cli_empty.log
        FAIL=$((FAIL + 1))
    fi
fi

# --- Test 4: --infile_list together with a positional trace file is
# rejected (mutually exclusive - see print_usage()'s own note) ---
echo "Testing: infile_list_with_positional_arg_rejected"

if $SIMULATOR --infile_list "$TRACE_DIR/file_list.txt" "$TRACE_DIR/combined.csv" \
    --total_nodes 100 > /tmp/progressive_cli_both.log 2>&1; then
    echo "  ✗ FAIL - expected nonzero exit when both --infile_list and a positional file are given"
    FAIL=$((FAIL + 1))
else
    echo "  ✓ PASS"
    PASS=$((PASS + 1))
fi

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL PROGRESSIVE LOADING TESTS PASSED"
    exit 0
else
    echo "✗ SOME PROGRESSIVE LOADING TESTS FAILED"
    exit 1
fi
