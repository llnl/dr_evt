# DR_EVT Scheduler Test Suite

Comprehensive test suite for validating the SLURM-style backfilling scheduler implementation.

## Test Coverage

### 1. Basic Functionality
- Job submission, start, and completion
- Event ordering (submit → start → end)
- Sequential and concurrent execution

### 2. Resource Management
- Resource allocation on job start
- Resource deallocation on job completion
- No over-subscription (never exceed total nodes)
- Accurate free node tracking

### 3. Backfill Behavior
- **Success case**: Small jobs backfill when space available
- **Failure case**: Jobs wait when they don't fit, resources sit idle
- **Partial case**: Some jobs backfill, others wait

### 4. Stress Testing
- 30-job saturation test with 100% resource utilization
- Mixed job sizes (5-80 nodes)
- Mixed durations (40-1000s)
- Complex event interactions

## Running Tests

### Quick Test (Bash)

```bash
# From project root
./tests/run_tests.sh
```

**Output**:
```
Running: basic_sequential ... PASS
Running: concurrent_backfill ... PASS
Running: resource_return ... PASS (all resources returned)
...
Total tests:  8
Passed:       8
Failed:       0

All tests passed!
```

### Detailed Test (Python)

```bash
# From project root
./tests/test_scheduler.py

# Or with Python explicitly
python3 tests/test_scheduler.py
```

**Output**:
```
Basic Functionality Tests:
  test_basic_execution ... PASS

Resource Management Tests:
  test_resource_accounting ... PASS
  test_no_over_subscription ... PASS
...
Total:  7
Passed: 7
Failed: 0
```

## Prerequisites

### Build the Simulator
```bash
cd build
make -j4
cd ..
```

### Test Traces
Test traces are located in `test_traces/`:
- `epoch_pbatch.csv` - Basic 3-job sequential test
- `backfill_test.csv` - Concurrent execution (3 jobs)
- `backfill_window_success.csv` - Successful backfill
- `idle_resources.csv` - Failed backfill (resources idle)
- `backfill_blocked.csv` - Partial backfill
- `saturation_test.csv` - 30-job stress test

## Test Structure

### Bash Test Suite (`run_tests.sh`)
- Simple pass/fail validation
- Checks job counts (submitted vs completed)
- Validates resource return
- Fast execution (~1 second total)
- Colored output (green=pass, red=fail)

### Python Test Suite (`test_scheduler.py`)
- Detailed validation logic
- Parses simulator output
- Checks resource accounting step-by-step
- Validates job ordering
- More comprehensive assertions
- Execution time: ~2 seconds

## Test Results

Results are saved to `tests/results/`:
- `<test_name>.log` - Successful test output
- `<test_name>_FAILED.log` - Failed test output for debugging

## Adding New Tests

### Add to Bash Suite

Edit `tests/run_tests.sh`:

```bash
run_test "my_new_test" \
    "$TEST_DIR/my_trace.csv" \
    10 10 200 \  # 10 jobs, 10 completed, 200 nodes
    "--trace_format simple --timestamp_format epoch"
```

### Add to Python Suite

Edit `tests/test_scheduler.py`:

```python
def test_my_feature(runner: TestRunner):
    """Test my new feature"""
    metrics = runner.run_simulator("test_traces/my_trace.csv")
    
    # Add assertions
    runner.assert_equal(metrics['jobs_submitted'], 10, "Jobs submitted")
    # ...

# In main():
runner.test("test_my_feature", lambda: test_my_feature(runner))
```

## Continuous Integration

To integrate with CI/CD:

```yaml
# .github/workflows/test.yml
- name: Build
  run: |
    mkdir -p build
    cd build
    cmake ..
    make -j4

- name: Run Tests
  run: |
    ./tests/run_tests.sh
    python3 tests/test_scheduler.py
```

## Test Failures

### Debug Failed Tests

1. Check the failure log:
   ```bash
   cat tests/results/<test_name>_FAILED.log
   ```

2. Run the specific test manually:
   ```bash
   ./build/simulator test_traces/<trace>.csv \
     --total_nodes 100 \
     --backfill_policy easy \
     --priority_policy fcfs \
     --runtime_mode actual \
     --trace_format simple \
     --timestamp_format epoch
   ```

3. Check for:
   - Resource over-subscription
   - Jobs not completing
   - Incorrect event ordering
   - Scheduler crashes

## Known Limitations

### Current Test Coverage
- ✅ EASY backfill policy
- ✅ FCFS priority policy
- ✅ USE_ACTUAL runtime mode
- ✅ Simple trace format (epoch timestamps)
- ✅ Resource saturation

### Not Yet Tested
- ⚠️ Conservative backfill policy
- ⚠️ SJF/LJF priority policies
- ⚠️ USE_LIMIT runtime mode (time limits)
- ⚠️ Lassen format (33 columns)
- ⚠️ ISO timestamp format with timezones
- ⚠️ Very large scale (1000+ jobs)

## Performance Benchmarks

Expected test execution times:
- Bash suite: ~1 second (8 tests)
- Python suite: ~2 seconds (7 tests)
- Full test run: ~3 seconds

If tests are significantly slower:
- Check for infinite loops in scheduler
- Verify event queue is being processed
- Check for resource deadlocks

## Troubleshooting

### "Simulator not found"
```bash
# Build the project first
cd build
cmake ..
make -j4
cd ..
```

### "Test traces not found"
```bash
# Verify test_traces/ directory exists
ls test_traces/

# Should contain: epoch_pbatch.csv, backfill_test.csv, etc.
```

### All tests fail with "timeout"
- Check if simulator has infinite loop
- Increase timeout in Python tests (default: 10s)
- Run simulator manually to see where it hangs

### Resource accounting errors
- Check for resource leaks in scheduler
- Verify `m_free_nodes` is updated correctly
- Check that all jobs release resources on completion

## Contributing

When adding new scheduler features:

1. Write test case first (test-driven development)
2. Add test to both bash and Python suites
3. Run full test suite before committing
4. Document expected behavior in test function

## References

- [TEST_SUMMARY.md](../TEST_SUMMARY.md) - Comprehensive test campaign results
- [BACKFILL_VERIFICATION.md](../BACKFILL_VERIFICATION.md) - Backfill behavior analysis
- [SATURATION_TEST_RESULTS.md](../SATURATION_TEST_RESULTS.md) - Stress test results
