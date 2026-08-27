#!/bin/bash
#
# Build all DR_EVT documentation
#
# Generates:
# - Doxygen API documentation (C++ source)
# - Sphinx documentation (general/Python)
#

set -e  # Exit on error

echo "========================================"
echo "Building DR_EVT Documentation"
echo "========================================"
echo ""

# Check if we're in the docs directory
if [ ! -f "Doxyfile" ]; then
    echo "Error: Must run from docs/ directory"
    echo "Usage: cd docs && ./build_docs.sh"
    exit 1
fi

# Check for doxygen
if ! command -v doxygen &> /dev/null; then
    echo "Warning: doxygen not found"
    echo "Install with: brew install doxygen (macOS) or apt-get install doxygen (Linux)"
    echo "Skipping Doxygen build..."
    SKIP_DOXYGEN=1
fi

# Check for sphinx
if ! command -v sphinx-build &> /dev/null; then
    echo "Warning: sphinx-build not found"
    echo "Install with: pip3 install sphinx sphinx-rtd-theme myst-parser"
    echo "Skipping Sphinx build..."
    SKIP_SPHINX=1
fi

echo "--- Building Doxygen Documentation ---"
if [ -z "$SKIP_DOXYGEN" ]; then
    # Clean old build
    rm -rf api/html api/latex

    # Generate documentation
    echo "Running doxygen..."
    doxygen Doxyfile 2>&1 | grep -i "warning" || true

    if [ -f "api/html/index.html" ]; then
        echo "✓ Doxygen documentation generated successfully"
        echo "  Output: docs/api/html/index.html"
    else
        echo "✗ Doxygen build failed"
        exit 1
    fi
else
    echo "✗ Skipping Doxygen (not installed)"
fi

echo ""
echo "--- Building Sphinx Documentation ---"
if [ -z "$SKIP_SPHINX" ]; then
    cd sphinx

    # Clean old build
    rm -rf _build

    # Generate documentation
    echo "Running sphinx-build..."
    sphinx-build -b html . _build/html 2>&1 | grep -i "warning" || true

    cd ..

    if [ -f "sphinx/_build/html/index.html" ]; then
        echo "✓ Sphinx documentation generated successfully"
        echo "  Output: docs/sphinx/_build/html/index.html"
    else
        echo "✗ Sphinx build failed"
        exit 1
    fi
else
    echo "✗ Skipping Sphinx (not installed)"
fi

echo ""
echo "========================================"
echo "Documentation Build Complete"
echo "========================================"
echo ""

if [ -z "$SKIP_DOXYGEN" ]; then
    echo "API Documentation:"
    echo "  open api/html/index.html"
    echo ""
fi

if [ -z "$SKIP_SPHINX" ]; then
    echo "General Documentation:"
    echo "  open sphinx/_build/html/index.html"
    echo ""
fi

echo "Or start HTTP server:"
echo "  cd docs/api/html && python3 -m http.server 8000"
echo "  Visit: http://localhost:8000"
echo ""

# Check for warnings
if [ -z "$SKIP_DOXYGEN" ]; then
    warning_count=$(doxygen Doxyfile 2>&1 | grep -ci "warning" || echo "0")
    if [ "$warning_count" != "0" ]; then
        echo "⚠️  Found $warning_count Doxygen warnings"
        echo "   Run 'doxygen Doxyfile' to see details"
    fi
fi

exit 0
