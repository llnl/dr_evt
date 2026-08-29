# Building and Testing C++ Streaming Tests

## Prerequisites

The streaming tests have been added to CMakeLists.txt:
- test_streaming_api - Basic API functionality tests
- test_batch_vs_streaming - Comprehensive batch vs streaming validation
- test_mpi_streaming (requires MPI) - MPI-coordinated distributed streaming

## Build Instructions

### 1. Clean and Reconfigure

```bash
cd build
rm -rf CMakeCache.txt CMakeFiles/
cmake ..
```

This will pick up the new test targets from CMakeLists.txt.

### 2. Build Streaming Tests

```bash
# Build all streaming tests
make test_streaming_api-bin test_batch_vs_streaming-bin -j4

# Build MPI test (requires MPI installed)
make test_mpi_streaming-bin

# Or build everything
make -j4
```

### 3. Verify Tests Built

```bash
ls -lh build/test_streaming_api
ls -lh build/test_batch_vs_streaming
ls -lh build/test_mpi_streaming  # If MPI available
```

Expected output:
```
-rwxr-xr-x  build/test_streaming_api
-rwxr-xr-x  build/test_batch_vs_streaming
-rwxr-xr-x  build/test_mpi_streaming
```

## Running Tests

### Run Individual Test

```bash
# Test 1: Basic Streaming API
./build/test_streaming_api

# Test 2: Batch vs Streaming Comprehensive Validation
./build/test_batch_vs_streaming tests/test_traces/scale/huge_2000jobs.csv

# Test 3: MPI Streaming (requires MPI)
mpirun -np 4 ./build/test_mpi_streaming tests/test_traces/scale/large_200jobs.csv
```

### Run via Test Runner

```bash
# All feature tests (includes C++ tests)
./tests/run_feature_tests.sh
```

The test runner will:
1. Check if binaries exist in `./build/`
2. Run them if found
3. Skip with message if not built

## MPI Requirement

`test_mpi_streaming` requires MPI:

**Install MPI (Ubuntu/Debian):**
```bash
sudo apt-get install libopenmpi-dev openmpi-bin
```

**Install MPI (macOS):**
```bash
brew install open-mpi
```

**Verify MPI:**
```bash
mpirun --version
```

Without MPI, CMake will skip building `test_mpi_streaming`.

## Troubleshooting

### Tests Not Building

**Problem:** `make test_streaming_api` says "No rule to make target"

**Solution:** Reconfigure CMake:
```bash
cd build
rm CMakeCache.txt
cmake ..
make -j4
```

### MPI Test Not Building

**Problem:** `test_mpi_streaming` not built

**Check:** Did CMake find MPI?
```bash
cd build
cmake .. | grep MPI
```

Should see:
```
-- Found MPI_C: ...
-- Found MPI_CXX: ...
```

If not found, install MPI and reconfigure.

### Tests Fail at Runtime

**Check:** Is the simulator library found?
```bash
ldd build/test_streaming_api
```

If missing, set `LD_LIBRARY_PATH`:
```bash
export LD_LIBRARY_PATH=./build:$LD_LIBRARY_PATH
./build/test_streaming_api
```

## GitHub CI

The streaming tests will automatically run in GitHub CI:

1. CI installs MPI (libopenmpi-dev, openmpi-bin)
2. CMake configures and finds MPI
3. All 3 streaming tests build
4. `run_feature_tests.sh` runs them
5. Results shown in CI logs

See `.github/workflows/tests.yml` for CI configuration.

## Test Descriptions

### test_streaming_api
Basic functional tests of the streaming API (insert_job, run_until_inclusive, run_until_exclusive).
Tests resource accounting, time advancement semantics, and scheduler correctness.

### test_batch_vs_streaming
Comprehensive validation that streaming mode produces identical results to batch mode.
Compares both job traces (scheduling decisions) and resource traces (resource accounting).
Tested with large workloads (2000+ jobs).

### test_mpi_streaming
Tests MPI-coordinated streaming where multiple ranks feed different subsets of jobs.
Validates that all ranks produce identical output (deterministic) and matches batch mode.
Uses MPI_Allreduce for time coordination across ranks.

## Status

**CMakeLists.txt:** ✓ Updated with all 3 tests  
**GitHub CI:** ✓ Configured with MPI  
**Documentation:** ✓ Complete  
**Tests:** ✓ All passing

### Validation Results

✅ **test_streaming_api**: All 4 tests pass  
✅ **test_batch_vs_streaming**: Validated with 2000 jobs - job traces AND resource traces match perfectly  
✅ **test_mpi_streaming**: All ranks produce identical output, matches batch mode  

### Next Steps

```bash
cd build
cmake ..
make -j4

# Run all streaming tests
./build/test_streaming_api
./build/test_batch_vs_streaming tests/test_traces/scale/huge_2000jobs.csv
mpirun -np 4 ./build/test_mpi_streaming tests/test_traces/scale/large_200jobs.csv
```
