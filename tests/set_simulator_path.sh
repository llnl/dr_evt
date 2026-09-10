#!/bin/bash
# Common function to find the installed simulator - source this in test scripts.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX:-$REPO_ROOT/install}"

if [ -n "${SIMULATOR:-}" ] && [ -x "${SIMULATOR}" ]; then
    : # Explicit simulator path takes precedence.
elif [ -x "$INSTALL_PREFIX/bin/simulator" ]; then
    SIMULATOR="$INSTALL_PREFIX/bin/simulator"
else
    echo "Error: installed simulator not found or not executable: $INSTALL_PREFIX/bin/simulator"
    echo "Install the project first, or set CMAKE_INSTALL_PREFIX to its install prefix."
    exit 1
fi

export SIMULATOR
