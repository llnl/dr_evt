# Installation

## Prerequisites

### Required
- C++17 compatible compiler (GCC 7+, Clang 5+)
- CMake 3.12+
- MPI library (OpenMPI, MPICH, or similar)

### Optional
- Python 3.8+ (for reference implementation and verification)
- Protobuf (for configuration file support)

## Building from Source

### Quick Build

```bash
# Clone repository
git clone https://github.com/LLNL/dr_evt.git
cd dr_evt

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Binary location
./simulator --help
```

### Build Options

```bash
# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Release build (optimized)
cmake -DCMAKE_BUILD_TYPE=Release ..
```

(An earlier version of this document also listed a `-DBUILD_TESTING=ON`
/ `make test` option here - there's no corresponding CTest integration in
`CMakeLists.txt`, so that example never worked. See "Verification" below
for how to actually run the test suite.)

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

### MPI not found

```bash
# Ubuntu/Debian
sudo apt-get install libopenmpi-dev

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

## Next Steps

- [Quick Start Guide](quickstart.md) - Run your first simulation
- [Tutorial](tutorial.md) - Step-by-step examples
- [User Guide](../user-guide/overview.md) - Complete documentation
