#!/usr/bin/env bash
# Run the gRPC backfill-window API test against a fresh local server.
#
# The test executable also retains its append-job coverage; its third test is
# GetBackfillWindowRequest, which verifies a FCFS/EASY reservation snapshot
# with releases at (50, 40) and (100, 60).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

SERVER="${CMAKE_INSTALL_PREFIX:-./install}/bin/dr_evt_server"
TEST_BIN="${CMAKE_INSTALL_PREFIX:-./install}/bin/tests/test_grpc_streaming_api"

TRACE="tests/test_traces/feature/empty_trace.csv"
PORT="${DR_EVT_BACKFILL_WINDOW_TEST_PORT:-53211}"

if [[ ! -x "$SERVER" || ! -x "$TEST_BIN" ]]; then
    echo "Missing dr_evt_server or test_grpc_streaming_api." >&2
    echo "Build with: cmake --build build --target dr_evt_server-bin test_grpc_streaming_api-bin" >&2
    exit 1
fi

RUN_DIR="$(mktemp -d "${TMPDIR:-/tmp}/dr-evt-backfill-window.XXXXXXXX")"
SERVER_PID=""
cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$RUN_DIR"
}
trap cleanup EXIT

"$SERVER" "127.0.0.1:${PORT}" >"$RUN_DIR/server.log" 2>&1 &
SERVER_PID=$!
sleep 1

echo "Running GetBackfillWindowRequest test on 127.0.0.1:${PORT}"
"$TEST_BIN" "127.0.0.1:${PORT}" "$TRACE"
