# Streaming API Implementation Status

## Summary

Streaming API methods have been implemented but tests reveal bugs in the simulation loop logic.

## Completed

✓ **API Methods Implemented** (in `src/sim/sim.hpp`):
- `insert_job(job_idx, submit_time)` - Alias for `submit_job()`
- `run_until_inclusive(target_time)` - Calls `advance_to(target_time)`
- `run_until_exclusive(target_time)` - Advances to just before target_time

✓ **CMakeLists.txt Updated**:
- Added `test_streaming_api-bin`
- Added `test_streaming_vs_batch-bin`
- Added `test_two_stream_manual-bin` (conditional on MPI)

✓ **GitHub CI Updated**:
- Installed OpenMPI (libopenmpi-dev, openmpi-bin)
- Tests will build and run in CI

✓ **Test Compilation**:
- `test_streaming_api` builds successfully
- `test_streaming_vs_batch` has minor issue (m_outfile is private)

## Issues Found

### Bug in `advance_to()` Function

The `advance_to(sim_time_t target_time)` function has a logic bug:

**Symptom**: When calling `advance_to(50)`, it continues past t=50 and recursively calls itself with larger target times (t=150).

**Debug output**:
```
[advance_to(50)] Loop 0: t=0, wait_queue=1, replay_queue=1
[advance_to(150)] Loop 1: t=50, wait_queue=0, replay_queue=2  <-- Wrong!
[advance_to(150)] Loop 2: t=100, wait_queue=0, replay_queue=1
```

**Root cause**: Unknown - possibly:
1. Recursive call from within `advance_to` (e.g., from `m_trace.run_until_inclusive`)
2. Event processing creating new events that trigger another advance
3. Logic error in the main event loop

**Impact**: Test fails with assertion:
```
assert(sim.get_nodes_in_use() == 30);  // Expected 30, got 0
```

### Test Trace Format Issue

The streaming API tests need simulation-mode traces (without begin_time/end_time), but may need additional fixes to duration/time_limit handling.

**Fixed**: Changed trace format to not include begin_time/end_time
**Still testing**: Whether duration is correctly computed from time_limit

### test_streaming_vs_batch Compilation Error

```cpp
params.m_outfile = "batch_output.csv";  // ERROR: m_outfile is private
```

**Fix needed**: Either:
1. Add public setter: `params.set_outfile("batch_output.csv")`
2. Make m_outfile public
3. Use a different approach to set output file

## Remaining Work

### Priority 1: Fix advance_to() Bug

1. Add more detailed tracing to `advance_to()` to find where the recursive call comes from
2. Check if `m_trace.run_until_inclusive()` or `m_trace.insert_job()` call `advance_to()`
3. Fix the logic so `advance_to(50)` stops at exactly t=50

### Priority 2: Fix test_streaming_vs_batch

1. Fix m_outfile access issue
2. Build and test

### Priority 3: Test with MPI

1. Install MPI if not available
2. Build `test_two_stream_manual`
3. Run with `mpirun -np 2`

## Files Modified

- `src/sim/sim.hpp` - Added insert_job(), run_until_inclusive(), run_until_exclusive()
- `src/sim/sim.cpp` - Fixed while loop condition (removed `|| !m_wait_queue.empty()`)
- `tests/test_streaming_api.cpp` - Fixed trace format (removed begin_time/end_time)
- `CMakeLists.txt` - Added all 3 streaming test targets
- `.github/workflows/tests.yml` - Added MPI installation
- `.github/workflows/quick-test.yml` - Added MPI installation

## Next Steps

1. **Debug advance_to()**: Find and fix the recursive call issue
2. **Complete test fixes**: Fix m_outfile issue in test_streaming_vs_batch
3. **Verify all tests pass**: Run complete test suite
4. **Remove debug output**: Clean up std::cerr debug statements once working
5. **Update documentation**: Document streaming API once stable

## Test Commands

```bash
# Build streaming tests
source venv/bin/activate
cd build
make test_streaming_api-bin test_streaming_vs_batch-bin -j4

# Run tests
./build/test_streaming_api
./build/test_streaming_vs_batch
mpirun -np 2 ./build/test_two_stream_manual

# Or via test runner (once working)
./tests/run_feature_tests.sh
```

## Notes

The streaming API is conceptually correct - the methods exist and have the right signatures. The bug is in the underlying `advance_to()` simulation loop logic, not in the API wrapper methods themselves.

Once `advance_to()` is fixed to properly stop at target_time, the streaming API should work as expected.

Last updated: 2026-08-28
