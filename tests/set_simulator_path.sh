#!/bin/bash
# Common function to find simulator - source this in test scripts

if [ -n "${CMAKE_INSTALL_PREFIX}" ] && [ -f "${CMAKE_INSTALL_PREFIX}/bin/simulator" ]; then
    SIMULATOR="${CMAKE_INSTALL_PREFIX}/bin/simulator"
elif [ -f "./build/simulator" ]; then
    SIMULATOR="./build/simulator"
else
    echo "Error: Simulator not found. Tried:"
    echo "  - \${CMAKE_INSTALL_PREFIX}/bin/simulator (if CMAKE_INSTALL_PREFIX is set)"
    echo "  - ./build/simulator (build directory)"
    echo ""
    echo "Build and install first:"
    echo "  cd build && cmake .. && make && make install"
    echo "Or set SIMULATOR environment variable to the binary path."
    exit 1
fi

export SIMULATOR
