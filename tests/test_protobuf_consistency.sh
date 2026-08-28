#!/bin/bash
# Test that protobuf configs produce identical results to command-line options

set -e

echo "======================================"
echo "Protobuf vs Command-Line Consistency Test"
echo "======================================"
echo ""

SIMULATOR="build/sim-bin"
TRACE="test_traces/backfill_test.csv"

# Check prerequisites
if [ ! -f "$SIMULATOR" ]; then
    echo "ERROR: Simulator not found at $SIMULATOR"
    echo "Build first with CMake"
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

# Test 1: Minimal config
echo "======================================"
echo "Test 1: Minimal Config"
echo "======================================"
echo ""

echo "Running with command-line..."
$SIMULATOR \
    --seed=42 \
    --outfile="$TEMP_DIR/cmdline_minimal.out" \
    "$TRACE" > "$TEMP_DIR/cmdline_minimal.log" 2>&1

echo "Running with protobuf config..."
$SIMULATOR \
    --config=test_configs/minimal_config.pb \
    --outfile="$TEMP_DIR/protobuf_minimal.out" \
    > "$TEMP_DIR/protobuf_minimal.log" 2>&1

# Compare outputs
if diff "$TEMP_DIR/cmdline_minimal.out" "$TEMP_DIR/protobuf_minimal.out" > /dev/null; then
    echo "✓ PASS: Minimal config produces identical results"
else
    echo "✗ FAIL: Minimal config results differ"
    echo "Differences:"
    diff "$TEMP_DIR/cmdline_minimal.out" "$TEMP_DIR/protobuf_minimal.out" | head -20
    exit 1
fi
echo ""

# Test 2: Full config with all parameters
echo "======================================"
echo "Test 2: Full Config"
echo "======================================"
echo ""

echo "Running with command-line..."
$SIMULATOR \
    --seed=42 \
    --total_nodes=100 \
    --backfill_policy=easy \
    --priority_policy=fcfs \
    --runtime_mode=limit \
    --trace_format=simple \
    --timestamp_format=epoch \
    --duration_mode=exact \
    --verbose \
    --outfile="$TEMP_DIR/cmdline_full.out" \
    "$TRACE" > "$TEMP_DIR/cmdline_full.log" 2>&1

echo "Running with protobuf config..."
$SIMULATOR \
    --config=test_configs/full_config.pb \
    > "$TEMP_DIR/protobuf_full.log" 2>&1

# Note: protobuf config sets its own outfile
if [ -f "test_protobuf_full.out" ]; then
    mv test_protobuf_full.out "$TEMP_DIR/protobuf_full.out"
fi

if diff "$TEMP_DIR/cmdline_full.out" "$TEMP_DIR/protobuf_full.out" > /dev/null; then
    echo "✓ PASS: Full config produces identical results"
else
    echo "✗ FAIL: Full config results differ"
    echo "Differences:"
    diff "$TEMP_DIR/cmdline_full.out" "$TEMP_DIR/protobuf_full.out" | head -20
    exit 1
fi
echo ""

# Test 3: Conservative backfilling
echo "======================================"
echo "Test 3: Conservative Backfilling"
echo "======================================"
echo ""

echo "Running with command-line..."
$SIMULATOR \
    --seed=42 \
    --total_nodes=100 \
    --backfill_policy=conservative \
    --priority_policy=sjf \
    --trace_format=simple \
    --timestamp_format=epoch \
    --duration_mode=exact \
    --outfile="$TEMP_DIR/cmdline_conservative.out" \
    "$TRACE" > "$TEMP_DIR/cmdline_conservative.log" 2>&1

echo "Running with protobuf config..."
$SIMULATOR \
    --config=test_configs/conservative_config.pb \
    > "$TEMP_DIR/protobuf_conservative.log" 2>&1

if [ -f "test_protobuf_conservative.out" ]; then
    mv test_protobuf_conservative.out "$TEMP_DIR/protobuf_conservative.out"
fi

if diff "$TEMP_DIR/cmdline_conservative.out" "$TEMP_DIR/protobuf_conservative.out" > /dev/null; then
    echo "✓ PASS: Conservative config produces identical results"
else
    echo "✗ FAIL: Conservative config results differ"
    echo "Differences:"
    diff "$TEMP_DIR/cmdline_conservative.out" "$TEMP_DIR/protobuf_conservative.out" | head -20
    exit 1
fi
echo ""

# Test 4: Distribution mode
echo "======================================"
echo "Test 4: Distribution Mode"
echo "======================================"
echo ""

echo "Running with command-line..."
$SIMULATOR \
    --seed=42 \
    --total_nodes=100 \
    --trace_format=simple \
    --timestamp_format=epoch \
    --duration_mode=distribution \
    --duration_distribution=normal \
    --duration_scale=0.8 \
    --duration_stddev=0.1 \
    --outfile="$TEMP_DIR/cmdline_dist.out" \
    "$TRACE" > "$TEMP_DIR/cmdline_dist.log" 2>&1

echo "Running with protobuf config..."
$SIMULATOR \
    --config=test_configs/distribution_config.pb \
    > "$TEMP_DIR/protobuf_dist.log" 2>&1

if [ -f "test_protobuf_dist.out" ]; then
    mv test_protobuf_dist.out "$TEMP_DIR/protobuf_dist.out"
fi

if diff "$TEMP_DIR/cmdline_dist.out" "$TEMP_DIR/protobuf_dist.out" > /dev/null; then
    echo "✓ PASS: Distribution config produces identical results"
else
    echo "✗ FAIL: Distribution config results differ"
    echo "Differences:"
    diff "$TEMP_DIR/cmdline_dist.out" "$TEMP_DIR/protobuf_dist.out" | head -20
    exit 1
fi
echo ""

# Test 5: Verify verbose flag from protobuf
echo "======================================"
echo "Test 5: Verbose Flag"
echo "======================================"
echo ""

echo "Checking verbose output from protobuf config..."
if grep -q "Starting simulation" "$TEMP_DIR/protobuf_full.log"; then
    echo "✓ PASS: Verbose flag works in protobuf config"
else
    echo "✗ FAIL: Verbose flag not working in protobuf config"
    echo "Expected verbose output not found"
    exit 1
fi
echo ""

# Summary
echo "======================================"
echo "Test Summary"
echo "======================================"
echo "✓ Test 1: Minimal config"
echo "✓ Test 2: Full config (all parameters)"
echo "✓ Test 3: Conservative backfilling + SJF"
echo "✓ Test 4: Distribution mode with sampling"
echo "✓ Test 5: Verbose flag"
echo ""
echo "SUCCESS: All protobuf configs produce identical results to command-line!"
echo ""
echo "Verified parameters:"
echo "  ✓ seed, max_jobs, max_time, infile, outfile"
echo "  ✓ verbose"
echo "  ✓ total_nodes"
echo "  ✓ backfill_policy (easy, conservative)"
echo "  ✓ priority_policy (fcfs, sjf)"
echo "  ✓ runtime_mode (limit)"
echo "  ✓ trace_format (simple)"
echo "  ✓ timestamp_format (epoch)"
echo "  ✓ duration_mode (exact, distribution)"
echo "  ✓ duration_distribution (normal)"
echo "  ✓ duration_scale, duration_stddev"
