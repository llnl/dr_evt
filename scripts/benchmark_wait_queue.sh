#!/bin/bash
# Benchmark script for wait_queue optimization
# Tests deque<pair<job_no_t, bool>> performance on various batch sizes

set -e

SIMULATOR="./build/simulator"
OUTDIR="/tmp/benchmark_output"
mkdir -p "$OUTDIR"

echo "=========================================="
echo "Wait Queue Performance Benchmark"
echo "=========================================="
echo ""

# Test cases: (name, trace_file, nodes, expected_jobs)
declare -a TESTS=(
    "Small_50:tests/test_traces/correctness/medium_50jobs_input.csv:100:50"
    "Medium_100:tests/test_traces/correctness/cross_validation_100jobs_input.csv:100:100"
    "Large_500:tests/test_traces/correctness/large_500jobs_input.csv:200:500"
    "XLarge_2000:tests/test_traces/correctness/large_2000jobs_input.csv:500:2000"
)

echo "Running benchmarks..."
echo ""

for test in "${TESTS[@]}"; do
    IFS=: read -r name trace nodes expected <<< "$test"

    echo "Test: $name"
    echo "  Trace: $trace"
    echo "  Nodes: $nodes"
    echo "  Expected jobs: $expected"

    outfile="$OUTDIR/${name}_output.csv"

    # Run 3 times and take average
    total_time=0
    for run in 1 2 3; do
        start=$(date +%s%N)
        $SIMULATOR "$trace" --total_nodes "$nodes" --trace_format simple \
            --timestamp_format epoch --duration_mode exact --outfile "$outfile" > /dev/null 2>&1
        end=$(date +%s%N)

        elapsed_ms=$(( (end - start) / 1000000 ))
        total_time=$(( total_time + elapsed_ms ))

        echo "    Run $run: ${elapsed_ms}ms"
    done

    avg_time=$(( total_time / 3 ))
    echo "  Average: ${avg_time}ms"

    # Verify correctness
    completed=$(grep "Jobs completed:" "$outfile" 2>/dev/null | wc -l || echo "0")
    if [ -f "$outfile" ]; then
        actual_jobs=$(tail -n +2 "$outfile" | wc -l | tr -d ' ')
        if [ "$actual_jobs" = "$expected" ]; then
            echo "  ✓ Verified: $actual_jobs jobs"
        else
            echo "  ✗ MISMATCH: got $actual_jobs, expected $expected"
        fi
    fi

    echo ""
done

echo "=========================================="
echo "Benchmark Complete"
echo "=========================================="
echo ""
echo "Results summary:"
echo "  Small (50 jobs):     ~quick"
echo "  Medium (100 jobs):   ~moderate"
echo "  Large (500 jobs):    ~noticeable benefit expected"
echo "  XLarge (2000 jobs):  ~significant benefit expected"
echo ""
echo "Output files saved to: $OUTDIR"
