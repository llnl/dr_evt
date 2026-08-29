# Building and Testing C++ Streaming Tests

## Prerequisites

The streaming tests have been added to CMakeLists.txt:
- test_streaming_api
- test_streaming_vs_batch  
- test_two_stream_manual (requires MPI)

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
make test_streaming_api test_streaming_vs_batch test_two_stream_manual -j4

# Or build everything
make -j4
```

### 3. Verify Tests Built

```bash
ls -lh build/test_streaming*
ls -lh build/test_two_stream_manual
```

Expected output:
```
-rwxr-xr-x  build/test_streaming_api
-rwxr-xr-x  build/test_streaming_vs_batch
-rwxr-xr-x  build/test_two_stream_manual
```

## Running Tests

### Run Individual Test

```bash
# Test 1: Streaming API
./build/test_streaming_api

# Test 2: Streaming vs Batch
./build/test_streaming_vs_batch

# Test 3: Two Stream (requires MPI)
mpirun -np 2 ./build/test_two_stream_manual
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

`test_two_stream_manual` requires MPI:

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

Without MPI, CMake will skip building `test_two_stream_manual`.

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

**Problem:** `test_two_stream_manual` not built

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
Tests the streaming/online simulation API for feeding jobs dynamically.

### test_streaming_vs_batch
Compares streaming mode (jobs fed incrementally) vs batch mode (all jobs at once).

### test_two_stream_manual
Tests two MPI ranks feeding jobs independently (round-robin) using MPI for coordination.

## Status

**CMakeLists.txt:** ✓ Updated with all 3 tests  
**GitHub CI:** ✓ Configured with MPI  
**Documentation:** ✓ Complete  

**Next step:** Rebuild with cmake to test locally

```bash
cd build
rm CMakeCache.txt
cmake ..
make -j4
./tests/run_feature_tests.sh
```
