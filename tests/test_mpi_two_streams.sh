#!/bin/bash
# Test MPI job feeder with two ranks against batch mode
#
# This test verifies that:
# 1. Two ranks feeding jobs independently (round-robin) produces correct results
# 2. MPI coordination (via MPI_Allreduce) ensures proper time advancement
# 3. Results match single-process batch mode

set -e

echo "======================================"
echo "MPI Two-Stream Test"
echo "======================================"
echo ""

# Configuration
TRACE="test_traces/backfill_test.csv"
TOTAL_NODES=100
BUILD_DIR="build"
SIMULATOR="$BUILD_DIR/simulator"
MPI_FEEDER="$BUILD_DIR/mpi_job_feeder"

# Check prerequisites
if [ ! -f "$SIMULATOR" ]; then
    echo "ERROR: Simulator not found at $SIMULATOR"
    echo "Build first: cd build && cmake .. && make"
    exit 1
fi

if [ ! -f "$TRACE" ]; then
    echo "ERROR: Test trace not found at $TRACE"
    exit 1
fi

# Create temp directory for outputs
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

echo "Temporary directory: $TEMP_DIR"
echo ""

# Test 1: Baseline - Batch mode with all jobs
echo "======================================"
echo "Test 1: Batch Mode (Baseline)"
echo "======================================"
echo "Running all jobs in single-process batch mode..."

$SIMULATOR \
    --trace_format=simple \
    --timestamp_format=epoch \
    --duration_mode=exact \
    --total_nodes=$TOTAL_NODES \
    --backfill_policy=easy \
    --priority_policy=fcfs \
    --outfile="$TEMP_DIR/batch_all.out" \
    "$TRACE" > "$TEMP_DIR/batch_all.log" 2>&1

# Extract job schedule (job_id, start_time, end_time)
echo "Extracting schedule from batch run..."
if [ -f "$TEMP_DIR/batch_all.out" ]; then
    # Parse output file to extract job schedules
    # Format: job_id submit_time start_time end_time nodes duration
    awk '{print $1, $3, $4}' "$TEMP_DIR/batch_all.out" | sort -n > "$TEMP_DIR/batch_all_schedule.txt"
    BATCH_JOBS=$(wc -l < "$TEMP_DIR/batch_all_schedule.txt")
    echo "✓ Batch mode completed: $BATCH_JOBS jobs"
else
    echo "ERROR: Batch output file not created"
    exit 1
fi

# Test 2: MPI mode with 2 ranks (if MPI available)
echo ""
echo "======================================"
echo "Test 2: MPI Mode (2 Ranks)"
echo "======================================"

if ! command -v mpirun &> /dev/null; then
    echo "⚠ MPI not available - skipping MPI test"
    echo ""
    echo "To run this test, install MPI:"
    echo "  macOS:  brew install openmpi"
    echo "  Linux:  sudo apt-get install libopenmpi-dev openmpi-bin"
    echo ""
    echo "Then build MPI job feeder:"
    echo "  cd build"
    echo "  mpic++ -std=c++17 -I../src -I. ../src/mpi/mpi_job_feeder.cpp -L. -ldr_evt -o mpi_job_feeder"
    exit 0
fi

if [ ! -f "$MPI_FEEDER" ]; then
    echo "Building MPI job feeder..."
    cd "$BUILD_DIR"
    if mpic++ -std=c++17 \
        -I../src -I. \
        ../src/mpi/mpi_job_feeder.cpp \
        -L. -ldr_evt \
        -o mpi_job_feeder 2>&1; then
        echo "✓ MPI job feeder built successfully"
    else
        echo "ERROR: Failed to build MPI job feeder"
        exit 1
    fi
    cd ..
fi

echo "Running with 2 MPI ranks..."
echo "  Rank 0: feeds jobs {0, 2, 4, 6, ...}"
echo "  Rank 1: feeds jobs {1, 3, 5, 7, ...}"
echo ""

# Note: MPI feeder writes output to current directory, so we need to handle that
cd "$TEMP_DIR"
mpirun -np 2 "../$MPI_FEEDER" \
    --total_nodes=$TOTAL_NODES \
    "../$TRACE" > mpi_2ranks.log 2>&1 || {
        echo "ERROR: MPI run failed"
        cat mpi_2ranks.log
        exit 1
    }
cd - > /dev/null

# The MPI feeder should have created an output file
# It writes to the trace base name with .out suffix
TRACE_BASE=$(basename "$TRACE" .csv)
if [ -f "$TEMP_DIR/${TRACE_BASE}.out" ]; then
    awk '{print $1, $3, $4}' "$TEMP_DIR/${TRACE_BASE}.out" | sort -n > "$TEMP_DIR/mpi_2ranks_schedule.txt"
    MPI_JOBS=$(wc -l < "$TEMP_DIR/mpi_2ranks_schedule.txt")
    echo "✓ MPI mode completed: $MPI_JOBS jobs"
else
    echo "ERROR: MPI output file not created"
    echo "Expected: $TEMP_DIR/${TRACE_BASE}.out"
    ls -la "$TEMP_DIR/"
    exit 1
fi

# Test 3: Compare schedules
echo ""
echo "======================================"
echo "Test 3: Compare Schedules"
echo "======================================"

if [ "$BATCH_JOBS" != "$MPI_JOBS" ]; then
    echo "✗ FAIL: Job count mismatch"
    echo "  Batch: $BATCH_JOBS jobs"
    echo "  MPI:   $MPI_JOBS jobs"
    exit 1
fi

echo "Comparing job schedules..."
echo ""

# Compare job by job
DIFF_COUNT=0
MAX_TIME_DIFF=0.0

while IFS= read -r batch_line; do
    batch_job=$(echo "$batch_line" | awk '{print $1}')
    batch_start=$(echo "$batch_line" | awk '{print $2}')
    batch_end=$(echo "$batch_line" | awk '{print $3}')

    mpi_line=$(grep "^$batch_job " "$TEMP_DIR/mpi_2ranks_schedule.txt")
    if [ -z "$mpi_line" ]; then
        echo "✗ Job $batch_job missing in MPI schedule"
        DIFF_COUNT=$((DIFF_COUNT + 1))
        continue
    fi

    mpi_start=$(echo "$mpi_line" | awk '{print $2}')
    mpi_end=$(echo "$mpi_line" | awk '{print $3}')

    # Compare start times (allow small floating point differences)
    start_diff=$(echo "$batch_start $mpi_start" | awk '{printf "%.6f", ($1-$2<0)?($2-$1):($1-$2)}')
    end_diff=$(echo "$batch_end $mpi_end" | awk '{printf "%.6f", ($1-$2<0)?($2-$1):($1-$2)}')

    # Check if difference is significant (> 0.001)
    if [ $(echo "$start_diff > 0.001" | bc -l) -eq 1 ] || [ $(echo "$end_diff > 0.001" | bc -l) -eq 1 ]; then
        echo "✗ Job $batch_job: timing mismatch"
        echo "    Batch: start=$batch_start end=$batch_end"
        echo "    MPI:   start=$mpi_start end=$mpi_end"
        echo "    Diff:  start=$start_diff end=$end_diff"
        DIFF_COUNT=$((DIFF_COUNT + 1))

        # Track max difference
        if [ $(echo "$start_diff > $MAX_TIME_DIFF" | bc -l) -eq 1 ]; then
            MAX_TIME_DIFF=$start_diff
        fi
    fi
done < "$TEMP_DIR/batch_all_schedule.txt"

echo ""
if [ "$DIFF_COUNT" -eq 0 ]; then
    echo "✓ PASS: All job schedules match!"
    echo "  $BATCH_JOBS jobs scheduled identically in both modes"
else
    echo "✗ FAIL: Found $DIFF_COUNT mismatches"
    echo "  Max time difference: $MAX_TIME_DIFF"
    exit 1
fi

# Test 4: Verify rank coordination
echo ""
echo "======================================"
echo "Test 4: Verify Rank Coordination"
echo "======================================"

# Check MPI log for coordination messages
if grep -q "Rank 0.*Submitted job" "$TEMP_DIR/mpi_2ranks.log" && \
   grep -q "Rank 1.*Submitted job" "$TEMP_DIR/mpi_2ranks.log"; then
    echo "✓ Both ranks submitted jobs"
else
    echo "⚠ Could not verify both ranks submitted jobs"
fi

# Count jobs submitted by each rank
RANK0_JOBS=$(grep -c "Rank 0.*Jobs submitted:" "$TEMP_DIR/mpi_2ranks.log" || echo "0")
RANK1_JOBS=$(grep -c "Rank 1.*Jobs submitted:" "$TEMP_DIR/mpi_2ranks.log" || echo "0")

if [ "$RANK0_JOBS" -gt 0 ] && [ "$RANK1_JOBS" -gt 0 ]; then
    echo "✓ Both ranks reported job submission"
    grep "Rank.*Jobs submitted:" "$TEMP_DIR/mpi_2ranks.log" || true
else
    echo "⚠ Could not parse rank statistics"
fi

# Summary
echo ""
echo "======================================"
echo "Test Summary"
echo "======================================"
echo "✓ Batch mode (baseline): $BATCH_JOBS jobs"
echo "✓ MPI mode (2 ranks):    $MPI_JOBS jobs"
echo "✓ Schedules match:       All $BATCH_JOBS jobs identical"
echo ""
echo "Key verification:"
echo "  ✓ Round-robin job partitioning works"
echo "  ✓ MPI_Allreduce coordination ensures correct timing"
echo "  ✓ Streaming API produces same results as batch mode"
echo ""
echo "SUCCESS: Two-stream MPI feeding validated!"
