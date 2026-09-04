#!/bin/bash
# Comprehensive FCFS Testing: Differential Correctness + Performance Comparison
#
# Combines two testing modes:
# 1. Differential Testing: fcfs vs fcfs_alt (correctness verification)
# 2. Performance Testing: Python vs C++ fcfs vs C++ fcfs_alt

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

# Parse command-line options
MODE="both"  # Default: run both tests
VERBOSE=false

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Comprehensive FCFS testing: correctness and performance"
    echo ""
    echo "OPTIONS:"
    echo "  -c, --correctness    Run only correctness tests (fcfs vs fcfs_alt)"
    echo "  -p, --performance    Run only performance tests (Python vs C++ implementations)"
    echo "  -b, --both           Run both correctness and performance tests (default)"
    echo "  -v, --verbose        Show detailed output"
    echo "  -h, --help           Show this help message"
    echo ""
    echo "EXAMPLES:"
    echo "  $0                   # Run both tests"
    echo "  $0 --correctness     # Verify correctness only"
    echo "  $0 --performance     # Benchmark performance only"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--correctness)
            MODE="correctness"
            shift
            ;;
        -p|--performance)
            MODE="performance"
            shift
            ;;
        -b|--both)
            MODE="both"
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            print_usage
            ;;
        *)
            echo "Unknown option: $1"
            print_usage
            ;;
    esac
done

# Check prerequisites
if [ ! -f "${SIMULATOR:-./build/simulator}" ]; then
    echo "Error: ./build/simulator not found"
    echo "Build first: cd build && cmake .. && make"
    exit 1
fi

################################################################################
# PART 1: DIFFERENTIAL CORRECTNESS TESTING
################################################################################

run_correctness_tests() {
    echo "=========================================="
    echo "Part 1: Differential Correctness Testing"
    echo "=========================================="
    echo ""
    echo "Purpose: Verify scheduler correctness by comparing four"
    echo "independent FCFS implementations that should produce"
    echo "identical results:"
    echo "  - fcfs with --queue_impl circular (boost::circular_buffer-based, default)"
    echo "  - fcfs with --queue_impl deque (deque-based)"
    echo "  - fcfs_alt (multimap-based, differential testing)"
    echo "  - fcfs with --queue_impl block (block-based, optimized)"
    echo ""

    PASS=0
    FAIL=0
    TRACE_DIR="tests/test_traces/comprehensive"

    # Auto-detect run_time_mode based on file contents. --duration_mode
    # is never passed anywhere in this script, so it stays at its own
    # default, "limit", throughout. That matters because
    # duration_mode="actual" would make the scheduler ignore
    # run_time_mode entirely and just use the trace's own real run
    # time - "limit" is what makes whichever run_time_mode gets
    # detected below actually get used.
    detect_run_time_mode() {
        local test_file="$1"
        if head -1 "$test_file" | grep -q "actual_run_time"; then
            echo "column"
        else
            echo "exact"
        fi
    }

    # Test all 34 comprehensive tests
    for test_file in "$TRACE_DIR"/*.csv; do
        # Skip expected output files
        if [[ "$test_file" == *".expected"* ]]; then
            continue
        fi

        test_name=$(basename "$test_file" .csv)

        # Detect run_time_mode
        run_time_mode=$(detect_run_time_mode "$test_file")

        # Run with scheduler_fcfs (priority_policy=fcfs)
        if [ "$VERBOSE" = true ]; then
            echo "  Running fcfs: $test_name"
        fi
        ./build/simulator "$test_file" \
            --priority_policy fcfs \
            --queue_impl deque \
            --total_nodes 100 \
            --trace_format simple \
            --timestamp_format epoch \
            --run_time_mode "$run_time_mode" \
            --backfill_policy easy \
            --outfile "/tmp/fcfs_${test_name}.csv" \
            --resource_trace "/tmp/fcfs_${test_name}_resources.csv" \
            > /dev/null 2>&1

        # Run with scheduler_fcfs_alt (priority_policy=fcfs_alt)
        if [ "$VERBOSE" = true ]; then
            echo "  Running fcfs_alt: $test_name"
        fi
        ./build/simulator "$test_file" \
            --priority_policy fcfs_alt \
            --total_nodes 100 \
            --trace_format simple \
            --timestamp_format epoch \
            --run_time_mode "$run_time_mode" \
            --backfill_policy easy \
            --outfile "/tmp/fcfs_alt_${test_name}.csv" \
            --resource_trace "/tmp/fcfs_alt_${test_name}_resources.csv" \
            > /dev/null 2>&1

        # Run with block queue (priority_policy=fcfs, queue_impl=block)
        if [ "$VERBOSE" = true ]; then
            echo "  Running fcfs (block queue): $test_name"
        fi
        ./build/simulator "$test_file" \
            --priority_policy fcfs \
            --queue_impl block \
            --total_nodes 100 \
            --trace_format simple \
            --timestamp_format epoch \
            --run_time_mode "$run_time_mode" \
            --backfill_policy easy \
            --outfile "/tmp/fcfs_block_${test_name}.csv" \
            --resource_trace "/tmp/fcfs_block_${test_name}_resources.csv" \
            > /dev/null 2>&1

        # Run with circular queue (priority_policy=fcfs, queue_impl=circular)
        if [ "$VERBOSE" = true ]; then
            echo "  Running fcfs (circular queue): $test_name"
        fi
        ./build/simulator "$test_file" \
            --priority_policy fcfs \
            --queue_impl circular \
            --total_nodes 100 \
            --trace_format simple \
            --timestamp_format epoch \
            --run_time_mode "$run_time_mode" \
            --backfill_policy easy \
            --outfile "/tmp/fcfs_circular_${test_name}.csv" \
            --resource_trace "/tmp/fcfs_circular_${test_name}_resources.csv" \
            > /dev/null 2>&1

        # Compare job schedules (all four must match)
        SCHEDULE_MATCH_ALT=0
        SCHEDULE_MATCH_BLOCK=0
        SCHEDULE_MATCH_CIRCULAR=0
        RESOURCE_MATCH_ALT=0
        RESOURCE_MATCH_BLOCK=0
        RESOURCE_MATCH_CIRCULAR=0

        if ! diff -q "/tmp/fcfs_${test_name}.csv" "/tmp/fcfs_alt_${test_name}.csv" > /dev/null 2>&1; then
            SCHEDULE_MATCH_ALT=1
        fi

        if ! diff -q "/tmp/fcfs_${test_name}.csv" "/tmp/fcfs_block_${test_name}.csv" > /dev/null 2>&1; then
            SCHEDULE_MATCH_BLOCK=1
        fi

        if ! diff -q "/tmp/fcfs_${test_name}.csv" "/tmp/fcfs_circular_${test_name}.csv" > /dev/null 2>&1; then
            SCHEDULE_MATCH_CIRCULAR=1
        fi

        # Compare resource traces (all four must match)
        if ! diff -q "/tmp/fcfs_${test_name}_resources.csv" "/tmp/fcfs_alt_${test_name}_resources.csv" > /dev/null 2>&1; then
            RESOURCE_MATCH_ALT=1
        fi

        if ! diff -q "/tmp/fcfs_${test_name}_resources.csv" "/tmp/fcfs_block_${test_name}_resources.csv" > /dev/null 2>&1; then
            RESOURCE_MATCH_BLOCK=1
        fi

        if ! diff -q "/tmp/fcfs_${test_name}_resources.csv" "/tmp/fcfs_circular_${test_name}_resources.csv" > /dev/null 2>&1; then
            RESOURCE_MATCH_CIRCULAR=1
        fi

        # All must match
        if [ $SCHEDULE_MATCH_ALT -eq 0 ] && [ $SCHEDULE_MATCH_BLOCK -eq 0 ] && [ $SCHEDULE_MATCH_CIRCULAR -eq 0 ] && \
           [ $RESOURCE_MATCH_ALT -eq 0 ] && [ $RESOURCE_MATCH_BLOCK -eq 0 ] && [ $RESOURCE_MATCH_CIRCULAR -eq 0 ]; then
            echo "✓ $test_name (circular == deque == multimap == block)"
            PASS=$((PASS + 1))
        else
            echo "✗ $test_name - MISMATCH"
            if [ $SCHEDULE_MATCH_ALT -ne 0 ]; then
                echo "  Job schedules differ (deque vs multimap):"
                echo "  fcfs:     /tmp/fcfs_${test_name}.csv"
                echo "  fcfs_alt: /tmp/fcfs_alt_${test_name}.csv"
                [ "$VERBOSE" = true ] && diff "/tmp/fcfs_${test_name}.csv" "/tmp/fcfs_alt_${test_name}.csv" | head -10
            fi
            if [ $SCHEDULE_MATCH_BLOCK -ne 0 ]; then
                echo "  Job schedules differ (deque vs block):"
                echo "  fcfs:       /tmp/fcfs_${test_name}.csv"
                echo "  fcfs_block: /tmp/fcfs_block_${test_name}.csv"
                [ "$VERBOSE" = true ] && diff "/tmp/fcfs_${test_name}.csv" "/tmp/fcfs_block_${test_name}.csv" | head -10
            fi
            if [ $SCHEDULE_MATCH_CIRCULAR -ne 0 ]; then
                echo "  Job schedules differ (deque vs circular):"
                echo "  fcfs:          /tmp/fcfs_${test_name}.csv"
                echo "  fcfs_circular: /tmp/fcfs_circular_${test_name}.csv"
                [ "$VERBOSE" = true ] && diff "/tmp/fcfs_${test_name}.csv" "/tmp/fcfs_circular_${test_name}.csv" | head -10
            fi
            if [ $RESOURCE_MATCH_ALT -ne 0 ]; then
                echo "  Resource traces differ (deque vs multimap):"
                echo "  fcfs:     /tmp/fcfs_${test_name}_resources.csv"
                echo "  fcfs_alt: /tmp/fcfs_alt_${test_name}_resources.csv"
                [ "$VERBOSE" = true ] && diff "/tmp/fcfs_${test_name}_resources.csv" "/tmp/fcfs_alt_${test_name}_resources.csv" | head -10
            fi
            if [ $RESOURCE_MATCH_BLOCK -ne 0 ]; then
                echo "  Resource traces differ (deque vs block):"
                echo "  fcfs:       /tmp/fcfs_${test_name}_resources.csv"
                echo "  fcfs_block: /tmp/fcfs_block_${test_name}_resources.csv"
                [ "$VERBOSE" = true ] && diff "/tmp/fcfs_${test_name}_resources.csv" "/tmp/fcfs_block_${test_name}_resources.csv" | head -10
            fi
            if [ $RESOURCE_MATCH_CIRCULAR -ne 0 ]; then
                echo "  Resource traces differ (deque vs circular):"
                echo "  fcfs:          /tmp/fcfs_${test_name}_resources.csv"
                echo "  fcfs_circular: /tmp/fcfs_circular_${test_name}_resources.csv"
                [ "$VERBOSE" = true ] && diff "/tmp/fcfs_${test_name}_resources.csv" "/tmp/fcfs_circular_${test_name}_resources.csv" | head -10
            fi
            FAIL=$((FAIL + 1))
        fi
    done

    echo ""
    echo "=========================================="
    echo "Differential Test Results"
    echo "=========================================="
    echo "Passed: $PASS"
    echo "Failed: $FAIL"
    echo "Total:  $((PASS + FAIL))"
    echo ""

    if [ $FAIL -eq 0 ]; then
        echo "✅ ALL CORRECTNESS TESTS PASSED"
        echo ""
        echo "All four FCFS implementations produce identical results!"
        echo "  - fcfs --queue_impl circular (boost::circular_buffer, default)"
        echo "  - fcfs --queue_impl deque (deque)"
        echo "  - fcfs_alt (multimap)"
        echo "  - fcfs --queue_impl block (BlockWaitQueue)"
        echo ""
        echo "This provides strong evidence of correctness."
        return 0
    else
        echo "❌ SOME CORRECTNESS TESTS FAILED"
        echo ""
        echo "Implementations differ - at least one has a bug."
        echo "Review the diff output above to investigate."
        return 1
    fi
}

################################################################################
# PART 2: PERFORMANCE COMPARISON
################################################################################

run_performance_tests() {
    echo ""
    echo "=========================================="
    echo "Part 2: Performance Comparison"
    echo "=========================================="
    echo ""
    echo "Testing 3 implementations:"
    echo "  1. Python reference (scheduler logic)"
    echo "  2. C++ fcfs (deque-based queue)"
    echo "  3. C++ fcfs_alt (multimap-based queue)"
    echo ""

    # Check if Python script exists
    if [ ! -f "scripts/python_reference_scheduler.py" ]; then
        echo "Warning: scripts/python_reference_scheduler.py not found"
        echo "Skipping performance comparison"
        return 0
    fi

    # Test files - comprehensive and scale tests
    TEST_FILES=(
        "tests/test_traces/comprehensive/01_backfill_allowed.csv"
        "tests/test_traces/comprehensive/21_sustained_high_load.csv"
        "tests/test_traces/scale/small_10jobs.csv"
        "tests/test_traces/scale/medium_50jobs.csv"
        "tests/test_traces/scale/large_100jobs.csv"
        "tests/test_traces/scale/large_200jobs.csv"
        "tests/test_traces/scale/xlarge_500jobs.csv"
    )

    # Create results file
    CSV_FILE="/tmp/performance_results.csv"

    echo "test,jobs,python_time,fcfs_time,fcfs_alt_time,python_vs_fcfs,python_vs_alt" > "$CSV_FILE"

    for test_file in "${TEST_FILES[@]}"; do
        if [ ! -f "$test_file" ]; then
            continue
        fi

        test_name=$(basename "$test_file" .csv)

        # Count jobs
        num_jobs=$(($(wc -l < "$test_file") - 1))

        echo ""
        echo "Testing: $test_name ($num_jobs jobs)"
        echo "----------------------------------------"

        # 1. Python reference implementation
        echo -n "  Python reference:  "
        PYTHON_START=$(date +%s.%N)
        python3 scripts/python_reference_scheduler.py "$test_file" 100 > /tmp/python_out.csv 2>/dev/null
        PYTHON_END=$(date +%s.%N)
        PYTHON_TIME=$(echo "$PYTHON_END - $PYTHON_START" | bc)
        printf "%8.4f sec\n" "$PYTHON_TIME"

        # 2. C++ fcfs (deque-based)
        echo -n "  C++ fcfs (deque):  "
        FCFS_START=$(date +%s.%N)
        # --duration_mode isn't passed, so it stays at its default,
        # "limit". That matters because duration_mode="actual" would
        # make the scheduler ignore --run_time_mode entirely and just
        # use the trace's own real run time - "limit" is what makes
        # --run_time_mode exact below (and the circular-queue run
        # further down) actually get used.
        ./build/simulator "$test_file" \
            --priority_policy fcfs \
            --total_nodes 100 \
            --trace_format simple \
            --timestamp_format epoch \
            --run_time_mode exact \
            --backfill_policy easy \
            --outfile /tmp/fcfs_out.csv \
            > /dev/null 2>&1
        FCFS_END=$(date +%s.%N)
        FCFS_TIME=$(echo "$FCFS_END - $FCFS_START" | bc)
        printf "%8.4f sec" "$FCFS_TIME"

        # Calculate speedup
        if (( $(echo "$PYTHON_TIME > 0" | bc -l) )); then
            FCFS_SPEEDUP=$(echo "scale=2; $PYTHON_TIME / $FCFS_TIME" | bc)
            printf " (%sx faster)\n" "$FCFS_SPEEDUP"
        else
            echo ""
        fi

        # 3. C++ fcfs_alt (multimap-based)
        echo -n "  C++ fcfs_alt (map): "
        ALT_START=$(date +%s.%N)
        ./build/simulator "$test_file" \
            --priority_policy fcfs_alt \
            --total_nodes 100 \
            --trace_format simple \
            --timestamp_format epoch \
            --run_time_mode exact \
            --backfill_policy easy \
            --outfile /tmp/fcfs_alt_out.csv \
            > /dev/null 2>&1
        ALT_END=$(date +%s.%N)
        ALT_TIME=$(echo "$ALT_END - $ALT_START" | bc)
        printf "%8.4f sec" "$ALT_TIME"

        # Calculate speedup
        if (( $(echo "$PYTHON_TIME > 0" | bc -l) )); then
            ALT_SPEEDUP=$(echo "scale=2; $PYTHON_TIME / $ALT_TIME" | bc)
            printf " (%sx faster)\n" "$ALT_SPEEDUP"
        else
            echo ""
        fi

        # Compare fcfs vs fcfs_alt
        if (( $(echo "$FCFS_TIME > 0" | bc -l) )); then
            if (( $(echo "$ALT_TIME < $FCFS_TIME" | bc -l) )); then
                DIFF=$(echo "scale=1; ($FCFS_TIME - $ALT_TIME) / $FCFS_TIME * 100" | bc)
                echo "  → fcfs_alt is ${DIFF}% faster than fcfs"
            elif (( $(echo "$ALT_TIME > $FCFS_TIME" | bc -l) )); then
                DIFF=$(echo "scale=1; ($ALT_TIME - $FCFS_TIME) / $FCFS_TIME * 100" | bc)
                echo "  → fcfs is ${DIFF}% faster than fcfs_alt"
            else
                echo "  → Same performance"
            fi
        fi

        # Save to CSV
        echo "$test_name,$num_jobs,$PYTHON_TIME,$FCFS_TIME,$ALT_TIME,$FCFS_SPEEDUP,$ALT_SPEEDUP" >> "$CSV_FILE"
    done

    echo ""
    echo "=========================================="
    echo "Performance Summary"
    echo "=========================================="
    echo ""
    echo "Results saved to: $CSV_FILE"
    echo ""

    # Display summary table
    if command -v column &> /dev/null; then
        cat "$CSV_FILE" | column -t -s,
    else
        cat "$CSV_FILE"
    fi

    echo ""
    echo "Key Findings:"
    echo "-------------"

    # Calculate average speedups
    AVG_FCFS=$(awk -F, 'NR>1 && $6 != "" {sum+=$6; count++} END {if(count>0) printf "%.1f", sum/count}' "$CSV_FILE")
    AVG_ALT=$(awk -F, 'NR>1 && $7 != "" {sum+=$7; count++} END {if(count>0) printf "%.1f", sum/count}' "$CSV_FILE")

    echo "• C++ fcfs is ${AVG_FCFS}x faster than Python on average"
    echo "• C++ fcfs_alt is ${AVG_ALT}x faster than Python on average"

    # Compare C++ implementations
    LAST_TEST=$(tail -1 "$CSV_FILE")
    LAST_JOBS=$(echo "$LAST_TEST" | cut -d, -f2)
    LAST_FCFS=$(echo "$LAST_TEST" | cut -d, -f4)
    LAST_ALT=$(echo "$LAST_TEST" | cut -d, -f5)

    if (( $(echo "$LAST_FCFS > 0" | bc -l) )); then
        if (( $(echo "$LAST_ALT < $LAST_FCFS" | bc -l) )); then
            DIFF=$(echo "scale=1; ($LAST_FCFS - $LAST_ALT) / $LAST_FCFS * 100" | bc)
            echo "• For large workloads ($LAST_JOBS jobs), fcfs_alt is ${DIFF}% faster"
            echo "  (multimap better for large queues)"
        elif (( $(echo "$LAST_ALT > $LAST_FCFS" | bc -l) )); then
            DIFF=$(echo "scale=1; ($LAST_ALT - $LAST_FCFS) / $LAST_FCFS * 100" | bc)
            echo "• For large workloads ($LAST_JOBS jobs), fcfs is ${DIFF}% faster"
            echo "  (deque better for large queues)"
        else
            echo "• Both C++ implementations perform similarly"
        fi
    fi

    echo ""
    echo "Implementation Details:"
    echo "----------------------"
    echo "• Python: Pure Python scheduler logic (baseline)"
    echo "• C++ fcfs: std::deque-based queue, linear scan for backfill"
    echo "• C++ fcfs_alt: std::multimap-based queue, ordered iteration"
    echo ""

    return 0
}

################################################################################
# MAIN EXECUTION
################################################################################

CORRECTNESS_RESULT=0
PERFORMANCE_RESULT=0

if [ "$MODE" = "correctness" ] || [ "$MODE" = "both" ]; then
    run_correctness_tests
    CORRECTNESS_RESULT=$?
fi

if [ "$MODE" = "performance" ] || [ "$MODE" = "both" ]; then
    run_performance_tests
    PERFORMANCE_RESULT=$?
fi

# Final summary
echo ""
echo "=========================================="
echo "FINAL SUMMARY"
echo "=========================================="
echo ""

if [ "$MODE" = "correctness" ]; then
    if [ $CORRECTNESS_RESULT -eq 0 ]; then
        echo "✅ Correctness: PASSED"
        exit 0
    else
        echo "❌ Correctness: FAILED"
        exit 1
    fi
elif [ "$MODE" = "performance" ]; then
    echo "✅ Performance: Complete"
    echo "   Results saved to /tmp/performance_results.csv"
    exit 0
else
    # Both tests
    if [ $CORRECTNESS_RESULT -eq 0 ]; then
        echo "✅ Correctness: PASSED"
    else
        echo "❌ Correctness: FAILED"
    fi
    echo "✅ Performance: Complete"
    echo ""

    if [ $CORRECTNESS_RESULT -eq 0 ]; then
        echo "🎉 All tests complete!"
        exit 0
    else
        echo "⚠️  Performance tests complete, but correctness tests failed"
        exit 1
    fi
fi
