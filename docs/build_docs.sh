#!/usr/bin/env bash
#
# Build the DR_EVT documentation site from the docs/ directory.
#
# The Makefile generates Doxygen XML when Doxygen is installed, then builds
# the single Sphinx site. Without Doxygen, the hand-written pages still build
# and the optional C++ API entry point explains that it is unavailable.

set -euo pipefail

if [[ ! -f "Makefile" || ! -f "conf.py" ]]; then
    echo "Run this script from the docs/ directory:" >&2
    echo "  cd docs && ./build_docs.sh" >&2
    exit 1
fi

make html "$@"

echo
echo "Documentation is available at _build/html/index.html"
