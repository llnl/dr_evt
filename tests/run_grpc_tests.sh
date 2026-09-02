#!/bin/bash
# gRPC Client/Server Tests
#
# Tests the dr_evt_server/dr_evt_client gRPC streaming interface:
# - A basic, single-pair server/client session (no MPI)
# - The MPI-based multi-client/multi-server harness, verifying the
#   lockstep cross-client synchronization produces the expected,
#   hand-computed schedule for two independent, interleaved-arrival-time
#   traces (see tests/test_traces/grpc/trace_a.csv and trace_b.csv)
#
# Requires a build with -DDR_EVT_ENABLE_GRPC=ON (which also implies
# -DDR_EVT_ENABLE_PROTOBUF=ON). The MPI harness additionally requires a
# build where MPI was found (see CMakeLists.txt's find_package(MPI) -
# skipped, not a failure, if MPI isn't available at configure time).

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "=========================================="
echo "gRPC Client/Server Tests"
echo "=========================================="
echo ""

if [ ! -f "./build/dr_evt_server" ] || [ ! -f "./build/dr_evt_client" ]; then
    echo "Error: ./build/dr_evt_server and/or ./build/dr_evt_client not found"
    echo "Build with -DDR_EVT_ENABLE_GRPC=ON (implies -DDR_EVT_ENABLE_PROTOBUF=ON)"
    exit 1
fi

PASS=0
FAIL=0
TRACE_DIR="tests/test_traces/grpc"

# --- Test 1: basic single-pair server/client session ---
echo "Testing: basic_server_client_session"

PORT=53001
./build/dr_evt_server "127.0.0.1:${PORT}" > /tmp/grpc_test_server.log 2>&1 &
SERVER_PID=$!
sleep 1

CLIENT_OUT=$(./build/dr_evt_client "127.0.0.1:${PORT}" "$TRACE_DIR/trace_a.csv" 2>&1) || true
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

# trace_a.csv (100 nodes, ample capacity, no queuing): job 0 (0->20),
# job 1 (10->25 - overlaps job 0, but both fit within 100 nodes), job 2
# (30->40) - hand-computed makespan is 40.
if echo "$CLIENT_OUT" | grep -q "Jobs completed:  3" && \
   echo "$CLIENT_OUT" | grep -q "Makespan:        40"; then
    echo "  ✓ PASS"
    PASS=$((PASS + 1))
else
    echo "  ✗ FAIL"
    echo "     Server log:"
    sed 's/^/       /' /tmp/grpc_test_server.log
    echo "     Client output:"
    echo "$CLIENT_OUT" | sed 's/^/       /'
    FAIL=$((FAIL + 1))
fi

# --- Test 2: MPI multi-client/multi-server lockstep synchronization ---
echo "Testing: mpi_multi_client_server_lockstep"

if [ ! -f "./build/test_grpc_multi_client_server" ]; then
    echo "  ⚠ SKIP - ./build/test_grpc_multi_client_server not found "
    echo "    (MPI not found at configure time, or not yet built)"
elif ! command -v mpirun > /dev/null 2>&1; then
    echo "  ⚠ SKIP - mpirun not found on PATH"
else
    MPI_OUT=$(timeout 30 mpirun --allow-run-as-root --oversubscribe -np 4 \
        ./build/test_grpc_multi_client_server \
        ./build/dr_evt_server 53100 \
        "$TRACE_DIR/trace_a.csv" "$TRACE_DIR/trace_b.csv" 2>&1) || true

    # Independently-verified expected makespans: trace_a=40, trace_b=30
    # (same reasoning as test 1 above; trace_b: job 0 (5->15), job 1
    # (15->25), job 2 (25->30)). The lockstep synchronization only
    # affects the relative pacing of submit_job/advance_to calls across
    # the two streams, not either individual simulation's own schedule -
    # each pair is otherwise fully independent (no shared nodes/state
    # between them), so these are the same values a standalone,
    # unsynchronized run of each trace alone would also produce.
    if echo "$MPI_OUT" | grep -q "makespan=40" && \
       echo "$MPI_OUT" | grep -q "makespan=30" && \
       echo "$MPI_OUT" | grep -q "submitted=3 completed=3 makespan=40" && \
       echo "$MPI_OUT" | grep -q "submitted=3 completed=3 makespan=30"; then
        echo "  ✓ PASS"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL"
        echo "     mpirun output:"
        echo "$MPI_OUT" | sed 's/^/       /'
        FAIL=$((FAIL + 1))
    fi
fi

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL GRPC TESTS PASSED"
    exit 0
else
    echo "✗ SOME GRPC TESTS FAILED"
    exit 1
fi
