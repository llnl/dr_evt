#!/bin/bash
# Append-Job Tests
#
# Trace::append_job()/Simulation::append_job() is the real streaming
# insertion point: a brand-new Job_Record for a job the trace has never
# seen before, as opposed to submit_job()/insert_job() (covered by
# run_streaming_tests.sh), which both only operate on a job already
# sitting in a preloaded m_data. See
# docs/dev/design-decisions/OUT_TRACE_STREAMING.md for the design.
#
# This verifies:
# 1. (C++ API) A trace with zero preloaded jobs, then jobs appended one
#    at a time and run to completion correctly.
# 2. (C++ API) append_job() reclaims a completed job's slot before
#    growing, at the actual point of need (not load_data(), which never
#    needs this - see the design doc's "Reclaim at the point of need"
#    section for why).
# 3. (C++ API) append_job() enforces the same submit_time >=
#    current_time precondition submit_job() already does.
# 4. (gRPC, if built) The same append scenario as (1), but over
#    the actual network wire via AppendJobRequest, against a real
#    running dr_evt_server.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "=========================================="
echo "Append-Job Tests"
echo "=========================================="
echo ""

PASS=0
FAIL=0

# --- Test: C++ API (test_append_job_api.cpp's own 3 sub-tests) ---
echo "Testing: append_job_api (C++ level)"

# Test binaries are installed under bin/tests/ (see CMakeLists.txt's
# separate install() rule for DR_EVT_UNIT_TEST_TARGETS), not directly
# under bin/ like the main simulator/tracer/dr_evt_server/dr_evt_client
# binaries - check there first.
APPEND_API_BIN="${CMAKE_INSTALL_PREFIX:-./install}/bin/tests/test_append_job_api"
if [ ! -f "$APPEND_API_BIN" ]; then
    APPEND_API_BIN="${CMAKE_INSTALL_PREFIX:-./install}/bin/test_append_job_api"
fi
if [ ! -f "$APPEND_API_BIN" ]; then
    APPEND_API_BIN="./build/test_append_job_api"
fi

if [ ! -f "$APPEND_API_BIN" ]; then
    echo "  ✗ FAIL - test_append_job_api binary not found"
    echo "    Build first: cd build && cmake .. && make test_append_job_api-bin"
    FAIL=$((FAIL + 1))
else
    if "$APPEND_API_BIN" > /tmp/append_job_api_out.txt 2>&1; then
        echo "  ✓ PASS"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL"
        sed 's/^/    /' /tmp/append_job_api_out.txt
        FAIL=$((FAIL + 1))
    fi
fi

# --- Test: gRPC (AppendJobRequest over the actual wire) ---
echo ""
echo "Testing: append_job_grpc (over the actual gRPC wire)"

SERVER="${CMAKE_INSTALL_PREFIX:-./install}/bin/dr_evt_server"
GRPC_TEST_BIN="${CMAKE_INSTALL_PREFIX:-./install}/bin/tests/test_append_job_grpc"
if [ ! -f "$GRPC_TEST_BIN" ]; then
    GRPC_TEST_BIN="${CMAKE_INSTALL_PREFIX:-./install}/bin/test_append_job_grpc"
fi
if [ ! -f "$GRPC_TEST_BIN" ]; then
    GRPC_TEST_BIN="./build/test_append_job_grpc"
fi
EMPTY_TRACE="tests/test_traces/feature/empty_trace.csv"

if [ ! -f "$SERVER" ] || [ ! -f "$GRPC_TEST_BIN" ]; then
    echo "  ⚠ SKIP - dr_evt_server and/or test_append_job_grpc not found"
    echo "    (build with -DDR_EVT_ENABLE_GRPC=ON to include this test)"
else
    PORT=53201
    "$SERVER" "127.0.0.1:${PORT}" > /tmp/append_job_grpc_server.log 2>&1 &
    SERVER_PID=$!
    sleep 1

    GRPC_OUT=$("$GRPC_TEST_BIN" "127.0.0.1:${PORT}" "$EMPTY_TRACE" 2>&1) || true
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true

    if echo "$GRPC_OUT" | grep -q "^PASSED$"; then
        echo "  ✓ PASS"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL"
        echo "     Server log:"
        sed 's/^/       /' /tmp/append_job_grpc_server.log
        echo "     Client output:"
        echo "$GRPC_OUT" | sed 's/^/       /'
        FAIL=$((FAIL + 1))
    fi
fi

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL APPEND-JOB TESTS PASSED"
    exit 0
else
    echo "✗ SOME APPEND-JOB TESTS FAILED"
    exit 1
fi
