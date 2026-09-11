# Installation

## Prerequisites

### Required
 + **Platforms**: Linux-based systems (macOS may work but not officially supported)
 + **C++ compiler**: C++17 support (GCC 7+, Clang 5+, or newer)
 + **CMake**: 3.24 or later
 + **Boost**: Components required: `regex`, `filesystem`, `system`, `program_options`, `serialization`, `container`, `multi_index`, `circular_buffer`
   - Tested with Boost 1.70+
   - Install: `apt-get install libboost-all-dev` (Ubuntu/Debian) or `brew install boost` (macOS)

### Optional (for full features)

**[Protocol Buffers](https://developers.google.com/protocol-buffers)**: For `--config` files (`-DDR_EVT_ENABLE_PROTOBUF=ON`) - not needed for a plain build
- Auto-downloaded if not found, or use `-DPROTOBUF_ROOT=<path>`

**Catch2 v2**: For the legacy Catch2-based unit tests (`-DDR_EVT_WITH_UNIT_TESTING=ON`)
- Existing tests use the Catch2 v2 single-header API (`catch2/catch.hpp`)
- DR_EVT uses an existing compatible Catch2 v2 installation when available; otherwise it fetches pinned Catch2 v2.13.10

**Python 3.7+**: For Python bindings (`-DDR_EVT_BUILD_PYTHON=ON`)
- Python development headers required: `apt-get install python3-dev`
- pybind11 auto-downloaded via FetchContent if not found

**[gRPC](https://grpc.io/)**: For online simulation service (`-DDR_EVT_ENABLE_GRPC=ON`)
- **Auto-download**: If not found, gRPC (with bundled Protobuf) is auto-downloaded via FetchContent (~5-10 min first build). Can OOM under full parallelism on memory-constrained machines - see "Livermore Computing (LC) HPC systems" below.
- **Manual install**: `apt-get install libgrpc++-dev protobuf-compiler-grpc` (Ubuntu/Debian)
- **Important**: gRPC includes its own Protobuf. If gRPC is enabled, you don't need separate Protobuf install.

**MPI**: For multi-client/server test harness only (optional even with gRPC)
- Install: `apt-get install libopenmpi-dev openmpi-bin`
- Install the Python MPI binding for the launcher: `python3 -m pip install mpi4py`

### Protocol Buffers & gRPC Details

**Protobuf usage:** Configuration file parsing ([proto3 syntax](https://developers.google.com/protocol-buffers/docs/proto3))
- Not built at all unless requested - pass `-DDR_EVT_ENABLE_PROTOBUF=ON` (or `-DDR_EVT_ENABLE_GRPC=ON`, which implies it) to enable
- If gRPC is enabled, Protobuf comes bundled with gRPC (no separate install needed)
- If gRPC is **not** enabled but `-DDR_EVT_ENABLE_PROTOBUF=ON` is, standalone Protobuf is auto-downloaded via FetchContent if not found

**Key relationship:**
```
gRPC build → includes Protobuf (bundled)
Protobuf-only build → standalone Protobuf installation
```

## Building from Source

### Quick Build

```bash
# Clone repository
git clone https://github.com/LLNL/dr_evt.git
cd dr_evt

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run from the build directory
./simulator --help
```

**Note**: The plain build shown above only needs Boost - it does not build
Protobuf or gRPC (both are opt-in, see below). If Boost isn't found on your
system, the first build downloads and compiles it via FetchContent
(~10-15 minutes); enabling Protobuf and/or gRPC adds their own download/build
time on top of that if they aren't found either. Subsequent builds are fast.

**CMake warnings**: You will see deprecation warnings from third-party dependencies (Boost, pybind11). These are harmless and come from their old cmake_minimum_required versions. To suppress them:
```bash
cmake .. -Wno-author -Wno-dev -DDR_EVT_BUILD_PYTHON=ON
```

### CMake Configuration Options

**Boost:**
```bash
cmake .. -DBOOST_ROOT=/path/to/boost
# or use environment variable
export BOOST_ROOT=/path/to/boost

# Skip system paths (useful if system install is broken or mismatched by version)
cmake .. -DAVOID_SYSTEM_BOOST=ON
```

**Protobuf (standalone, when gRPC not used):**
```bash
cmake .. -DPROTOBUF_ROOT=/path/to/protobuf

# Skip system paths (useful if system install is broken or mismatched by version)
cmake .. -DDR_EVT_ENABLE_PROTOBUF=ON -DAVOID_SYSTEM_PROTOBUF=ON
```

**gRPC:**
```bash
# Enable gRPC support (auto-enables Protobuf)
cmake .. -DDR_EVT_ENABLE_GRPC=ON

# Skip system path search (useful if system install is broken or mismatched by version)
cmake .. -DDR_EVT_ENABLE_GRPC=ON -DAVOID_SYSTEM_GRPC=ON

# Reuse an explicit/local gRPC install, otherwise fall back to FetchContent
cmake .. -DDR_EVT_ENABLE_GRPC=ON -DAVOID_SYSTEM_GRPC=ON \
  -DCMAKE_INSTALL_PREFIX=/path/to/local/prefix
```

**Python bindings:**
```bash
# Enable Python bindings
cmake .. -DDR_EVT_BUILD_PYTHON=ON

# Specify Python executable
cmake .. -DDR_EVT_BUILD_PYTHON=ON -DPython3_EXECUTABLE=/path/to/python3
```

**Testing:**
```bash
# Register ordinary CTest tests
cmake .. -DBUILD_TESTING=ON

# Enable the Catch2-based unit-test framework
cmake .. -DDR_EVT_WITH_UNIT_TESTING=ON
```

`BUILD_TESTING` controls ordinary CTest registration. `DR_EVT_WITH_UNIT_TESTING`
enables the separate Catch2-based unit-test framework.

**Build type:**
```bash
# Debug build (symbols, no optimization)
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Release build (optimized, default)
cmake .. -DCMAKE_BUILD_TYPE=Release
```

**Complete example with all features:**
```bash
mkdir build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DDR_EVT_ENABLE_GRPC=ON \
  -DDR_EVT_BUILD_PYTHON=ON \
  -DBOOST_ROOT=/opt/homebrew/opt/boost
make -j$(nproc)
```
(if gRPC isn't installed, this falls back to the same from-source
build discussed under "Livermore Computing (LC) HPC systems" below)

**Livermore Computing (LC) HPC systems:**
```bash
mkdir build && cd build
cmake .. \
  -DDR_EVT_ENABLE_GRPC=ON \
  -DDR_EVT_BUILD_PYTHON=ON \
  -DAVOID_SYSTEM_GRPC=ON \
  -DAVOID_SYSTEM_BOOST=ON \
  -DCMAKE_INSTALL_PREFIX=$(realpath ../install)
make -j4
make install

# Set up environment
export CMAKE_INSTALL_PREFIX=$(realpath ../install)
export PATH=${CMAKE_INSTALL_PREFIX}/bin:$PATH
export PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/python:$PYTHONPATH
```

The `AVOID_SYSTEM_*` options prevent ABI mismatches with system-installed
libraries (common on HPC systems with multiple compiler toolchains). They
still permit dependencies in explicit/local prefixes; dependencies not found
there are built via FetchContent. A from-source gRPC build can OOM on
memory-constrained nodes under full parallelism, with output like:

```
make[2]: *** [.../boringssl_gtest.dir/build.make:90: .../gtest-all.cc.o] Killed
make[1]: *** [CMakeFiles/Makefile2:12579: .../boringssl_gtest.dir/all] Error 2
```

`Killed` means memory pressure, not a compiler error. `-j4` above is deliberately conservative for this reason (~2 GB/job is a reasonable estimate for gRPC); lower it further if you still hit this.
When using pre-built gRPC, `make -j$(nproc)` should still be ok.

## Installation

```bash
# Install to system (requires sudo)
sudo make install

# Or install to custom location
cmake -DCMAKE_INSTALL_PREFIX=/path/to/install ..
make install
```

## Python Environment

`scripts/python_reference_scheduler.py` - the reference EASY-backfilling
implementation the C++ simulator's comprehensive test suite is checked
against (see [Testing Guide](../TESTING_GUIDE.md)) - has no third-party
dependencies at all; it only imports from the Python 3 standard library
(`csv`, `dataclasses`, `heapq`, etc.). No virtualenv or `pip install` is
needed to run it.

A separate `docs/requirements.txt` exists, but it's for building this
Sphinx documentation site itself (`sphinx`, `myst-parser`, etc.) - unrelated
to running or testing the simulator. Create and activate a dedicated virtual
environment before installing those documentation dependencies:

```bash
python3 -m venv .venv-docs
source .venv-docs/bin/activate
python -m pip install -r docs/requirements.txt
make -C docs html
```

`make install` in `docs/` uses the active Python environment; it does not
create a virtual environment itself.

If you're building the optional [Python bindings](../api/PYTHON_API.md)
(`-DDR_EVT_BUILD_PYTHON=ON`), that's a compiled extension module, not a
pip package - see that page for how to make it importable
(`PYTHONPATH`), not a `pip install` step.

## Verification

Run tests to verify installation:

```bash
# Selected CTest coverage, including the Pcon data model
cd build
ctest -R 'test_pcon_trace|test_trace_type_cli' --output-on-failure

# Comprehensive scheduler-correctness suite (34 tests)
cd ../tests
./run_scheduler_correctness_tests.sh
```

Should see:
```
🎉 ALL TESTS PASSED!
Passed:  34
Failed:  0
Missing: 0
Total:   34
```

(An earlier version of this document referenced `./test_all.sh` - no such
script exists, the actual name is `run_scheduler_correctness_tests.sh` - and a
`scripts/verify_against_analytical.py` step claiming "verified against
analytical oracles" - that script was an unused remnant of an abandoned
test-suite design and has been removed; "34 tests pass" means the C++
simulator matches a from-scratch Python reference implementation, not
independently-verified analytical ground truth. See
`docs/TESTING_GUIDE.md` for what that distinction means.)

## Troubleshooting

### CMake can't find Boost

```bash
# On macOS with Homebrew
cmake .. -DBOOST_ROOT=/opt/homebrew/opt/boost

# On Linux
cmake .. -DBOOST_ROOT=/usr/include/boost

# Or set environment variable
export BOOST_ROOT=/path/to/boost
cmake .. -DCMAKE_BUILD_TYPE=Release
```

### Build fails during Protobuf/gRPC download

```bash
# Check internet connection
# Or download manually and use:
cmake .. -DPROTOBUF_ROOT=/path/to/protobuf

# For gRPC, skip system search; use an explicit/local package or FetchContent:
cmake .. -DDR_EVT_ENABLE_GRPC=ON -DAVOID_SYSTEM_GRPC=ON
```

### gRPC/Protobuf version mismatch

```bash
# System install conflicts with FetchContent version
# Solution: Skip system path search
cmake .. -DDR_EVT_ENABLE_GRPC=ON -DAVOID_SYSTEM_GRPC=ON
```

### Python bindings fail to build

```bash
# Missing Python development headers
# Ubuntu/Debian
sudo apt-get install python3-dev

# macOS
brew install python3

# Specify Python version explicitly
cmake .. -DDR_EVT_BUILD_PYTHON=ON -DPython3_EXECUTABLE=$(which python3)
```

### MPI not found (optional dependency)

```bash
# Ubuntu/Debian
sudo apt-get install libopenmpi-dev openmpi-bin

# macOS
brew install open-mpi

# Set MPI path
export MPI_HOME=/path/to/mpi
cmake -DMPI_HOME=$MPI_HOME ..
```

### CMake version too old

```bash
# Ubuntu/Debian - get newer CMake
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc | sudo apt-key add -
sudo apt-add-repository 'deb https://apt.kitware.com/ubuntu/ focal main'
sudo apt-get update
sudo apt-get install cmake

# macOS
brew install cmake
```

### Compiler not C++17 compatible

```bash
# Ubuntu/Debian
sudo apt-get install g++-9
export CXX=g++-9

# macOS
xcode-select --install
```

### "No such file or directory" errors

Make sure you're in the build directory:
```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Next Steps

- [Quick Start Guide](quickstart.md) - Run your first simulation
- [Tutorial](tutorial.md) - Step-by-step examples
- [User Guide](../user-guide/overview.md) - Complete documentation
