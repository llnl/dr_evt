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

# With tests
cmake -DBUILD_TESTING=ON ..
make
make test
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
# Quick test suite
cd tests
./test_all.sh

# Full analytical verification
python3 ../scripts/verify_against_analytical.py
```

Should see:
```
✓ ALL TESTS PASSED
Both implementations verified against analytical oracles!
Total: 23/23 tests passed
```

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
