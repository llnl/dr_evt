# Unit Tests

## Overview

Unit tests verify basic functionality: input/output parsing, timestamp formats, simple scenarios, and basic execution. These tests check that the simulator can run without verifying scheduling correctness.

## Test List (8 tests)

### Timestamp Format Tests
1. **timestamp_epoch_format.csv** - Epoch timestamp parsing
2. **timestamp_epoch_simple.csv** - Simple epoch format
3. **timestamp_iso_format.csv** - ISO 8601 timestamp parsing
4. **timezone_handling.csv** - Timezone conversion

### Basic Execution Tests
5. **simple_basic.csv** - Minimal 2-job test
6. **simple_2jobs.csv** - Two job basic execution
7. **sequential_3jobs.csv** - Three jobs sequential execution
8. **sequential_3jobs.trace** - Trace format variant

## Purpose

Unit tests ensure:
- Input file parsing works
- Different timestamp formats are handled correctly
- Basic job execution completes
- Output is generated correctly

These tests do NOT verify:
- Scheduling correctness (see correctness tests)
- Backfilling algorithm (see correctness tests)
- Complex scenarios (see feature tests)

## Running Tests

```bash
# Run all unit tests
./tests/run_unit_tests.sh

# Run individual test
./build/simulator tests/test_traces/unit/simple_basic.csv \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --duration_mode exact \
    --outfile /tmp/output.csv
```

## Expected Behavior

All unit tests should:
- Complete without errors
- Produce output CSV file
- Parse timestamps correctly
- Execute jobs (not necessarily correctly scheduled)

## Test Location

```
tests/test_traces/unit/
├── sequential_3jobs.csv
├── sequential_3jobs.trace
├── simple_2jobs.csv
├── simple_basic.csv
├── timestamp_epoch_format.csv
├── timestamp_epoch_simple.csv
├── timestamp_iso_format.csv
└── timezone_handling.csv
```

## Adding Unit Tests

To add a new unit test:

1. Create input trace: `tests/test_traces/unit/<name>.csv`
2. Keep it simple (2-5 jobs)
3. Focus on testing specific I/O functionality
4. Run: `./tests/run_unit_tests.sh`

## Current Status

✓ 8/8 unit tests passing

Last verified: 2026-08-28
