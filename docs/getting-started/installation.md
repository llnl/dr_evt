# Installation

## Prerequisites

### Required
 + **Platforms**: Linux-based systems (macOS may work but not officially supported)
 + **C++ compiler**: C++20 concepts support (GCC 10+, Clang 13+, or newer)
 + **CMake**: 3.24 or later
 + **Boost**: Compiled components required: `program_options` and
   `serialization`; header-only APIs used: `graph`, `multi_index`, and
   `circular_buffer`
   - Tested with Boost 1.70+
   - Install: `apt-get install libboost-all-dev` (Ubuntu/Debian) or `brew install boost` (macOS)
 + **Ser20**: RNG-state serialization; an installed package is used when available, otherwise CMake uses `external/ser20` or fetches the pinned release

### Optional (for full features)

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

**[Protocol Buffers](https://developers.google.com/protocol-buffers)**: For `--config` files without gRPC (`-DDR_EVT_ENABLE_PROTOBUF=ON`) - not needed for a plain build
- Auto-downloaded if not found, or use `-DPROTOBUF_ROOT=<path>`

**MPI**: For multi-client/server test harness only (optional even with gRPC)
- Install: `apt-get install libopenmpi-dev openmpi-bin`
- Install the Python MPI binding for the launcher: `python3 -m pip install mpi4py`

### gRPC and Protocol Buffers

Enable gRPC directly when the client/server interface is needed. This selects
the Protobuf version supplied with gRPC and avoids mixing incompatible gRPC and
standalone Protobuf installations.

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

# Configure, build, and install
export CMAKE_INSTALL_PREFIX=/path/to/install
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}"
cmake --build build -j$(nproc)
cmake --install build

# Run the installed executable
${CMAKE_INSTALL_PREFIX}/bin/simulator --help
```

Livermore Computing users should follow the
[LC HPC system build instructions](#livermore-computing-lc-hpc-systems) below.

**Note**: The plain build shown above needs Boost and Ser20; it does not build
Protobuf or gRPC (both are opt-in, see below). If Boost or Ser20 is not found,
the first build obtains and compiles it via FetchContent. Boost can take
~10-15 minutes; enabling Protobuf and/or gRPC adds their own download/build
time when they are not found either. Subsequent builds reuse populated sources.

**CMake warnings**: You will see deprecation warnings from third-party dependencies (Boost, pybind11). These are harmless and come from their old cmake_minimum_required versions. To suppress them:
```bash
cmake -S . -B build -Wno-author -Wno-dev -DDR_EVT_BUILD_PYTHON=ON
```

### CMake Configuration Options

**Boost:**
```bash
cmake -S . -B build -DBOOST_ROOT=/path/to/boost
# or search one or more dependency prefixes
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/dependencies
# or use environment variable
export BOOST_ROOT=/path/to/boost

# Skip system paths (useful if system install is broken or mismatched by version)
cmake -S . -B build -DAVOID_SYSTEM_BOOST=ON
```

`CMAKE_PREFIX_PATH` remains active with `AVOID_SYSTEM_BOOST=ON`; only default
system locations are excluded.

**Ser20:**
```bash
cmake -S . -B build -DSER20_ROOT=/path/to/ser20
# or select the directory containing ser20Config.cmake directly
cmake -S . -B build -Dser20_DIR=/path/to/lib/cmake/ser20

# Skip default system paths while retaining explicit roots and prefixes
cmake -S . -B build -DAVOID_SYSTEM_SER20=ON
```

When no installed package is found, DR_EVT builds `external/ser20` through
FetchContent. If that checkout is absent, it downloads the pinned release.
Because Ser20 is part of DR_EVT's public interface, a fallback build installs
Ser20's library, headers, and CMake package files alongside DR_EVT.

**gRPC:**
```bash
# Enable gRPC support (auto-enables Protobuf)
cmake -S . -B build -DDR_EVT_ENABLE_GRPC=ON

# Skip system path search (useful if system install is broken or mismatched by version)
cmake -S . -B build -DDR_EVT_ENABLE_GRPC=ON -DAVOID_SYSTEM_GRPC=ON

# Reuse an explicit/local gRPC install, otherwise fall back to FetchContent
cmake -S . -B build -DDR_EVT_ENABLE_GRPC=ON -DAVOID_SYSTEM_GRPC=ON \
  -DCMAKE_INSTALL_PREFIX=/path/to/local/prefix
```

**Protobuf (standalone, only when gRPC is not used):**
```bash
cmake -S . -B build -DPROTOBUF_ROOT=/path/to/protobuf

# Skip system paths (useful if system install is broken or mismatched by version)
cmake -S . -B build -DDR_EVT_ENABLE_PROTOBUF=ON -DAVOID_SYSTEM_PROTOBUF=ON
```

**Python bindings:**
```bash
# Enable Python bindings
cmake -S . -B build -DDR_EVT_BUILD_PYTHON=ON

# Specify Python executable
cmake -S . -B build -DDR_EVT_BUILD_PYTHON=ON -DPython3_EXECUTABLE=/path/to/python3
```

**Testing:**
```bash
# Register ordinary CTest tests
cmake -S . -B build -DBUILD_TESTING=ON

# Enable the Catch2-based unit-test framework
cmake -S . -B build -DDR_EVT_WITH_UNIT_TESTING=ON
```

`BUILD_TESTING` controls ordinary CTest registration. `DR_EVT_WITH_UNIT_TESTING`
enables the separate Catch2-based unit-test framework.

**Build type:**
```bash
# Debug build (symbols, no optimization)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Release build (optimized, default)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

**Complete example with all features:**
```bash
export CMAKE_INSTALL_PREFIX=/path/to/install
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}" \
  -DDR_EVT_ENABLE_GRPC=ON \
  -DDR_EVT_BUILD_PYTHON=ON \
  -DBOOST_ROOT=/opt/homebrew/opt/boost
cmake --build build -j$(nproc)
cmake --install build
```
(if gRPC isn't installed, this falls back to the same from-source build
described in [Livermore Computing (LC) HPC systems](#livermore-computing-lc-hpc-systems))

### Livermore Computing (LC) HPC systems

```bash
export CMAKE_INSTALL_PREFIX=$(realpath install)
cmake -S . -B build \
  -DDR_EVT_ENABLE_GRPC=ON \
  -DDR_EVT_BUILD_PYTHON=ON \
  -DAVOID_SYSTEM_GRPC=ON \
  -DAVOID_SYSTEM_BOOST=ON \
  -DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}"
cmake --build build -j4
cmake --install build

# Set up environment
export DR_EVT_INSTALL_LIBDIR=lib  # Use the configured CMAKE_INSTALL_LIBDIR value (often lib64 on HPC systems)
export PATH=${CMAKE_INSTALL_PREFIX}/bin:$PATH
export PYTHONPATH=${CMAKE_INSTALL_PREFIX}/${DR_EVT_INSTALL_LIBDIR}/python:$PYTHONPATH
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
export CMAKE_INSTALL_PREFIX=/path/to/install
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}"
cmake --build build
cmake --install build
```

## Python environments

The Python reference scheduler uses only the standard library. The optional
compiled bindings and their import path are documented in the
[Python API](../api/PYTHON_API.md). Documentation dependencies and local
Sphinx build commands are maintained in
[Documentation Development](../README.md).

## Verification

Run CTest to verify the features enabled in the current build:

```bash
ctest --test-dir build --output-on-failure
```

The [Test Suite README](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#build-and-run)
lists focused regression runners and prerequisites. The
[Testing Guide](../TESTING_GUIDE.md) explains the validation methodology.

## Troubleshooting

### CMake can't find Boost

```bash
# On macOS with Homebrew
cmake -S . -B build -DBOOST_ROOT=/opt/homebrew/opt/boost

# On Linux
cmake -S . -B build -DBOOST_ROOT=/usr/include/boost

# Or set environment variable
export BOOST_ROOT=/path/to/boost
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

### Build fails during Protobuf/gRPC download

```bash
# Check internet connection
# Or download manually and use:
cmake -S . -B build -DPROTOBUF_ROOT=/path/to/protobuf

# For gRPC, skip system search; use an explicit/local package or FetchContent:
cmake -S . -B build -DDR_EVT_ENABLE_GRPC=ON -DAVOID_SYSTEM_GRPC=ON
```

### gRPC/Protobuf version mismatch

```bash
# System install conflicts with FetchContent version
# Solution: Skip system path search
cmake -S . -B build -DDR_EVT_ENABLE_GRPC=ON -DAVOID_SYSTEM_GRPC=ON
```

### Python bindings fail to build

```bash
# Missing Python development headers
# Ubuntu/Debian
sudo apt-get install python3-dev

# macOS
brew install python3

# Specify Python version explicitly
cmake -S . -B build -DDR_EVT_BUILD_PYTHON=ON \
  -DPython3_EXECUTABLE=$(command -v python3)
```

### MPI not found (optional dependency)

```bash
# Ubuntu/Debian
sudo apt-get install libopenmpi-dev openmpi-bin

# macOS
brew install open-mpi

# Set MPI path
export MPI_HOME=/path/to/mpi
cmake -S . -B build -DMPI_HOME="${MPI_HOME}"
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

### Compiler not C++20 compatible

```bash
# Ubuntu/Debian
sudo apt-get install g++-13
export CXX=g++-13

# macOS
xcode-select --install
```

### "No such file or directory" errors

Reconfigure and build from the repository root:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Next Steps

- [Quick Start Guide](quickstart.md) - Run your first simulation
- [Tutorial](tutorial.md) - Step-by-step examples
- [User Guide](../user-guide/overview.md) - Complete documentation
