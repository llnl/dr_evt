#!/bin/bash
# Replay Mode Tests
#
# Verifies that the tracer (standalone replay binary - no scheduler
# involved) faithfully reproduces the resource usage a simulator run
# already produced:
# 1. Run simulation mode (simulator) → job_trace + resource_trace_sim
# 2. Replay job_trace (tracer, using its begin_time/end_time directly)
#    → resource_trace_replay
# 3. Compare: resource_trace_sim == resource_trace_replay

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

# Source common simulator and tracer path finders
source "$SCRIPT_DIR/set_simulator_path.sh"
source "$SCRIPT_DIR/set_tracer_path.sh"

echo "=========================================="
echo "Replay Mode Tests"
echo "=========================================="
echo ""
echo "Testing that replay mode reproduces simulation resource usage"
echo ""


PASS=0
FAIL=0
OPTIONAL_OUTPUT_DIR=$(mktemp -d "/tmp/dr-evt-replay-optional.XXXXXXXX")
trap 'rm -rf -- "$OPTIONAL_OUTPUT_DIR"' EXIT INT TERM

# Exercise the internal replay reclamation boundaries before the CLI-level
# simulation/replay comparisons below.
RECLAMATION_BIN="${CMAKE_INSTALL_PREFIX:-./install}/bin/tests/test_replay_reclamation"

echo "Testing: replay job-store reclamation boundaries"
if [ ! -x "$RECLAMATION_BIN" ]; then
    echo "  ✗ FAIL - installed test_replay_reclamation binary not found"
    echo "    Expected: $RECLAMATION_BIN"
    FAIL=$((FAIL + 1))
elif "$RECLAMATION_BIN"; then
    echo "  ✓ PASS - replay reclamation boundaries"
    PASS=$((PASS + 1))
else
    echo "  ✗ FAIL - replay reclamation boundaries"
    FAIL=$((FAIL + 1))
fi

# Use several scheduler-correctness fixtures as replay test inputs
REPLAY_TESTS=(
    "01_backfill_allowed"
    "05_multiple_backfills"
    "13_consecutive_fcfs"
    "21_sustained_high_load"
)

for test_base in "${REPLAY_TESTS[@]}"; do
    echo "Testing: $test_base"

    input_trace="tests/test_traces/scheduler_correctness/${test_base}.csv"

    if [ ! -f "$input_trace" ]; then
        echo "  ✗ Input not found: $input_trace"
        FAIL=$((FAIL + 1))
        continue
    fi

    # Step 1: Run simulation mode
    sim_job_output="/tmp/replay_sim_${test_base}_jobs.csv"
    sim_resource_output="/tmp/replay_sim_${test_base}_resources.csv"

    # Use run_time_mode=limit (jobs run for full time_limit)
    $SIMULATOR "$input_trace" \
        --total_nodes 100 \
        --trace_format simple \
        --timestamp_format epoch \
        --run_time_mode limit \
        --outfile "$sim_job_output" \
        --resource_trace "$sim_resource_output" \
        > /dev/null 2>&1

    if [ ! -f "$sim_job_output" ] || [ ! -f "$sim_resource_output" ]; then
        echo "  ✗ Simulation failed"
        FAIL=$((FAIL + 1))
        continue
    fi

    # Step 2: Replay the job trace - tracer only, no scheduler involved
    default_output_dir="${OPTIONAL_OUTPUT_DIR}/default-${test_base}"
    mkdir -p "$default_output_dir"
    replay_resource_output="$default_output_dir/replay-resources.csv"

    (cd "$default_output_dir" && \
        $TRACER --infile "$sim_job_output" \
            --total_nodes 100 \
            --datfile /dev/null \
            --resource_trace "$replay_resource_output") \
            > /dev/null 2>&1

    if [ ! -f "$replay_resource_output" ]; then
        echo "  ✗ Replay failed"
        FAIL=$((FAIL + 1))
        continue
    fi

    default_file_set=$(find "$default_output_dir" -maxdepth 1 -type f \
        -printf '%f\n' | sort)
    if [ "$default_file_set" != "replay-resources.csv" ]; then
        echo "  ✗ Unexpected default output-file set"
        echo "$default_file_set" | sed 's/^/       /'
        FAIL=$((FAIL + 1))
        continue
    fi

    # One fixture also verifies the inverse: every optional report is
    # produced when explicitly requested. This is an integration behavior,
    # not merely a command-line parsing default.
    if [ "$test_base" = "01_backfill_allowed" ]; then
        explicit_output_dir="$OPTIONAL_OUTPUT_DIR/explicit"
        mkdir -p "$explicit_output_dir"
        explicit_job_output="$explicit_output_dir/jobs.csv"
        explicit_resource_output="$explicit_output_dir/resources.csv"
        explicit_sub_output="$explicit_output_dir/submissions.csv"
        explicit_summary_output="$explicit_output_dir/submission-summary.csv"
        expected_output_dir="$REPO_ROOT/tests/test_traces/replay"

        (cd "$explicit_output_dir" && \
            $TRACER --infile "$sim_job_output" \
                --total_nodes 100 \
                --datfile /dev/null \
                --outfile "$explicit_job_output" \
                --resource_trace "$explicit_resource_output" \
                --subfile "$explicit_sub_output" \
                --subsumf "$explicit_summary_output") \
                > /dev/null 2>&1

        explicit_file_set=$(find "$explicit_output_dir" -maxdepth 1 -type f \
            -printf '%f\n' | sort)
        expected_file_set=$(printf '%s\n' jobs.csv resources.csv \
            submission-summary.csv submissions.csv)
        if [ "$explicit_file_set" != "$expected_file_set" ]; then
            echo "  ✗ Unexpected explicitly enabled output-file set"
            echo "$explicit_file_set" | sed 's/^/       /'
            FAIL=$((FAIL + 1))
            continue
        fi

        reports_match=1
        diff -u "$expected_output_dir/optional_reports.expected_jobs.csv" \
            "$explicit_job_output" || reports_match=0
        diff -u "$expected_output_dir/optional_reports.expected_resources.csv" \
            "$explicit_resource_output" || reports_match=0
        diff -u "$expected_output_dir/optional_reports.expected_submissions.csv" \
            "$explicit_sub_output" || reports_match=0
        diff -u \
            "$expected_output_dir/optional_reports.expected_submission_summary.csv" \
            "$explicit_summary_output" || reports_match=0
        if [ "$reports_match" -ne 1 ]; then
            echo "  ✗ Optional replay report content differs from expected"
            FAIL=$((FAIL + 1))
            continue
        fi
    fi

    # Step 3: Compare resource traces
    if diff -q "$sim_resource_output" "$replay_resource_output" > /dev/null; then
        echo "  ✓ PASS - Resource traces match"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL - Resource traces differ"
        echo "    Simulation:  $sim_resource_output"
        echo "    Replay:      $replay_resource_output"
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL REPLAY TESTS PASSED"
    echo ""
    echo "Replay mode correctly reproduces simulation resource usage."
    exit 0
else
    echo "✗ SOME REPLAY TESTS FAILED"
    echo ""
    echo "Replay mode does not match simulation resource usage."
    exit 1
fi
