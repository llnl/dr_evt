# Installation

## Prerequisites

### Required
 + **Platforms**: Linux-based systems (macOS may work but not officially supported)
 + **C++ compiler**: C++17 support (GCC 7+, Clang 5+, or newer)
 + **CMake**: 3.24 or later
 + **Boost**: Components required: `regex`, `filesystem`, `system`, `program_options`, `serialization`, `container`, `multi_index`, `circular_buffer`
   - Tested with Boost 1.70+
   - Install: `apt-get install libboost-all-dev` (Ubuntu/Debian) or `brew install boost` (macOS)
 + [**Protocol Buffers**](https://developers.google.com/protocol-buffers): Auto-downloaded if not found, or use `-DPROTOBUF_ROOT=<path>`

### Optional (for full features)

**Python 3.7+**: For Python bindings (`-DDR_EVT_BUILD_PYTHON=ON`)
- Python development headers required: `apt-get install python3-dev`
- pybind11 auto-downloaded via FetchContent if not found

**[gRPC](https://grpc.io/)**: For online simulation service (`-DDR_EVT_ENABLE_GRPC=ON`)
- **Auto-download**: If not found, gRPC (with bundled Protobuf) is auto-downloaded via FetchContent (~5-10 min first build)
- **Manual install**: `apt-get install libgrpc++-dev protobuf-compiler-grpc` (Ubuntu/Debian)
- **Important**: gRPC includes its own Protobuf. If gRPC is enabled, you don't need separate Protobuf install.

**MPI**: For multi-client/server test harness only (optional even with gRPC)
- Install: `apt-get install libopenmpi-dev openmpi-bin`

### Protocol Buffers & gRPC Details

**Protobuf usage:** Configuration file parsing ([proto3 syntax](https://developers.google.com/protocol-buffers/docs/proto3))
- Enabled by default (`-DDR_EVT_ENABLE_PROTOBUF=ON`)
- If gRPC is enabled, Protobuf comes bundled with gRPC (no separate install needed)
- If gRPC is **not** enabled, standalone Protobuf is auto-downloaded via FetchContent if not found

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

# Binary location
./simulator --help
```

**Note**: First build downloads and compiles dependencies (~5-10 minutes if gRPC/Protobuf not installed). Subsequent builds are fast.

**CMake warnings**: You will see deprecation warnings from third-party dependencies (Boost, pybind11). These are harmless and come from their old cmake_minimum_required versions. To suppress them:
```bash
cmake .. -Wno-author -DDR_EVT_BUILD_PYTHON=ON
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

# Force FetchContent download (ignores system install)
cmake .. -DDR_EVT_ENABLE_GRPC=ON -DAVOID_SYSTEM_GRPC=ON
```

**Python bindings:**
```bash
# Enable Python bindings
cmake .. -DDR_EVT_BUILD_PYTHON=ON

# Specify Python executable
cmake .. -DDR_EVT_BUILD_PYTHON=ON -DPYTHON_EXECUTABLE=/path/to/python3
```

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

## Installation

```bash
# Install to system (requires sudo)
sudo make install

# Or install to custom location
cmake -DCMAKE_INSTALL_PREFIX=/path/to/install ..
make install
```

## Python Environment

For verification scripts and reference implementation:

```bash
# Create virtual environment
python3 -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate

# Install dependencies
pip install -r requirements.txt
```

## Verification

Run tests to verify installation:

```bash
# Comprehensive test suite (34 tests)
cd tests
./test_all_dr_evt.sh
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
script exists, the actual name is `test_all_dr_evt.sh` - and a
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

# For gRPC, skip system search and force FetchContent:
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
cmake .. -DDR_EVT_BUILD_PYTHON=ON -DPYTHON_EXECUTABLE=$(which python3)
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
