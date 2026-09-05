#!/bin/bash
# Common function to find tracer - source this in test scripts

if [ -n "${CMAKE_INSTALL_PREFIX}" ] && [ -f "${CMAKE_INSTALL_PREFIX}/bin/tracer" ]; then
    TRACER="${CMAKE_INSTALL_PREFIX}/bin/tracer"
elif [ -f "./build/tracer" ]; then
    TRACER="./build/tracer"
else
    echo "Error: Tracer not found. Tried:"
    echo "  - \${CMAKE_INSTALL_PREFIX}/bin/tracer (if CMAKE_INSTALL_PREFIX is set)"
    echo "  - ./build/tracer (build directory)"
    echo ""
    echo "Build and install first:"
    echo "  cd build && cmake .. && make && make install"
    echo "Or set TRACER environment variable to the binary path."
    exit 1
fi

export TRACER
