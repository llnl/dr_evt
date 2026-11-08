#!/bin/bash
# Append-Job Tests
#
# Trace::append_job()/Simulation::append_job() is the real streaming
# insertion point: a brand-new Job_Record for a job the trace has never
# seen before, as opposed to submit_job()/insert_job() (covered by
# run_streaming_tests.sh), which both only operate on a job already
# sitting in a preloaded m_data. See
# docs/dev/OUTPUT_TRACE_BUFFERS.md for the design.
#
# The in-process binary covers append, batch, capacity, advancement,
# accounting, planner endpoints, and memory pressure. When gRPC is built,
# the runner also executes wire-level append, monitoring, and warm-start batch
# checks against a real server.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/test_reporting.sh"
test_report_enable
INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX:-$REPO_ROOT/install}"
if [[ "$INSTALL_PREFIX" != /* ]]; then
    INSTALL_PREFIX="$REPO_ROOT/${INSTALL_PREFIX#./}"
fi

if ! RUN_DIR="$(mktemp -d "${TMPDIR:-/tmp}/dr-evt-append-job.XXXXXXXX" 2>/dev/null)"; then
    RUN_DIR="$(mktemp -d "/tmp/dr-evt-append-job.XXXXXXXX")"
fi
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf -- "$RUN_DIR"
}
test_report_set_cleanup cleanup

cd "$REPO_ROOT"

echo "=========================================="
echo "Append-Job Tests"
echo "=========================================="
echo ""

PASS=0
FAIL=0

# --- Test: C++ API (test_append_job_api.cpp's focused checks) ---
echo "Testing: append_job_api (C++ level)"

# Test binaries are installed under bin/tests/ (see CMakeLists.txt's
# separate install() rule for DR_EVT_UNIT_TEST_TARGETS), not directly
# under bin/ like the main simulator/tracer/dr_evt_server/dr_evt_client
# binaries - check there first.
APPEND_API_BIN="$INSTALL_PREFIX/bin/tests/test_append_job_api"

if [ ! -x "$APPEND_API_BIN" ]; then
    echo "  ✗ FAIL - installed test_append_job_api binary not found"
    echo "    Expected: $APPEND_API_BIN"
    FAIL=$((FAIL + 1))
else
    if (cd "$RUN_DIR" && "$APPEND_API_BIN") \
        > "$RUN_DIR/append_job_api_out.txt" 2>&1; then
        echo "  ✓ PASS"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL"
        sed 's/^/    /' "$RUN_DIR/append_job_api_out.txt"
        FAIL=$((FAIL + 1))
    fi
fi

# --- Test: gRPC (AppendJobRequest over the actual wire) ---
echo ""
echo "Testing: grpc_streaming_api (over the actual gRPC wire)"

SERVER="$INSTALL_PREFIX/bin/dr_evt_server"
GRPC_TEST_BIN="$INSTALL_PREFIX/bin/tests/test_grpc_streaming_api"
EMPTY_TRACE="$REPO_ROOT/tests/test_traces/feature/empty_trace.csv"

if [ ! -x "$SERVER" ] || [ ! -x "$GRPC_TEST_BIN" ]; then
    echo "  ⚠ SKIP - dr_evt_server and/or test_grpc_streaming_api not found"
    echo "    (build with -DDR_EVT_ENABLE_GRPC=ON to include this test)"
else
    PORT=53201
    (cd "$RUN_DIR" && exec "$SERVER" "127.0.0.1:${PORT}") \
        > "$RUN_DIR/append_job_grpc_server.log" 2>&1 &
    SERVER_PID=$!
    sleep 1

    if GRPC_OUT=$("$GRPC_TEST_BIN" "127.0.0.1:${PORT}" "$EMPTY_TRACE" 2>&1); then
        GRPC_STATUS=0
    else
        GRPC_STATUS=$?
    fi
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""

    if [ "$GRPC_STATUS" -eq 0 ] && echo "$GRPC_OUT" | grep -q "^PASSED$"; then
        echo "  ✓ PASS"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL"
        echo "     Server log:"
        sed 's/^/       /' "$RUN_DIR/append_job_grpc_server.log"
        echo "     Client output:"
        echo "       Exit status: $GRPC_STATUS"
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
