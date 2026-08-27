# DR_EVT Documentation

Complete documentation for the DR_EVT HPC Job Scheduler Simulator.

## Documentation Structure

```
docs/
├── USER_GUIDE.md              # Complete user guide
├── TEST_DESCRIPTIONS.md       # Detailed test explanations
├── Doxyfile                   # Doxygen configuration
├── README.md                  # This file
├── build_docs.sh             # Build all documentation
├── sphinx/                   # Sphinx documentation
│   ├── conf.py              # Sphinx configuration
│   └── index.rst            # Documentation index
└── api/                     # Generated API docs (Doxygen output)
    └── html/
        └── index.html       # API documentation entry point
```

## Available Documentation

### User Documentation

1. **[USER_GUIDE.md](USER_GUIDE.md)** - Complete user manual
   - Quick start examples
   - Command-line options
   - Trace file formats
   - Scheduler policies explained
   - Troubleshooting guide

2. **[../TESTING.md](../TESTING.md)** - Testing guide
   - Running test suite
   - Understanding test results
   - Adding new tests

3. **[TEST_DESCRIPTIONS.md](TEST_DESCRIPTIONS.md)** - Test explanations
   - What each test validates
   - Why tests are important
   - How to interpret failures

### Reference Documentation

4. **[../TRACE_FORMAT.md](../TRACE_FORMAT.md)** - Trace format specification
   - Simple format (7 columns)
   - Lassen format (33 columns)
   - Timestamp formats

5. **[../QUICKSTART.md](../QUICKSTART.md)** - Quick reference
   - Common commands
   - Quick examples

### Test Results

6. **[../TEST_SUMMARY.md](../TEST_SUMMARY.md)** - Comprehensive test results
7. **[../BACKFILL_VERIFICATION.md](../BACKFILL_VERIFICATION.md)** - Backfill validation
8. **[../SATURATION_TEST_RESULTS.md](../SATURATION_TEST_RESULTS.md)** - Stress test analysis

## Building Documentation

### Prerequisites

Install documentation tools:
```bash
# Doxygen (for C++ API docs)
# macOS
brew install doxygen graphviz

# Ubuntu/Debian
sudo apt-get install doxygen graphviz

# Sphinx (for Python/general docs)
pip3 install sphinx sphinx-rtd-theme myst-parser
```

### Build All Documentation

```bash
# From project root
./docs/build_docs.sh

# Or manually
cd docs

# Build Doxygen (C++ API)
doxygen Doxyfile

# Build Sphinx (Python/general)
cd sphinx
make html
cd ..
```

### View Documentation

```bash
# API documentation (Doxygen)
open docs/api/html/index.html

# General documentation (Sphinx)
open docs/sphinx/_build/html/index.html

# Or with Python HTTP server
cd docs/api/html && python3 -m http.server 8000
# Visit http://localhost:8000
```

## Documentation for Different Audiences

### End Users (Running Simulations)

Start with:
1. [USER_GUIDE.md](USER_GUIDE.md) - Complete usage guide
2. [../QUICKSTART.md](../QUICKSTART.md) - Quick examples
3. [../TRACE_FORMAT.md](../TRACE_FORMAT.md) - Creating trace files

### Testers (Validating Scheduler)

Start with:
1. [../TESTING.md](../TESTING.md) - Running tests
2. [TEST_DESCRIPTIONS.md](TEST_DESCRIPTIONS.md) - Understanding tests
3. [../TEST_SUMMARY.md](../TEST_SUMMARY.md) - Test results

### Developers (Modifying Code)

Start with:
1. `api/html/index.html` - Doxygen API documentation
2. [../BUILD_SETUP.md](../BUILD_SETUP.md) - Build instructions
3. Source code comments (Doxygen-formatted)

### Researchers (Understanding Algorithms)

Start with:
1. [USER_GUIDE.md](USER_GUIDE.md) - Scheduler policies section
2. [../BACKFILL_VERIFICATION.md](../BACKFILL_VERIFICATION.md) - Algorithm analysis
3. `src/sim/scheduler.hpp` - Implementation details

## Documentation Standards

### Code Comments (Doxygen)

C++ files use Doxygen format:
```cpp
/**
 * @brief Brief description
 *
 * Detailed description with examples.
 *
 * @param param_name Parameter description
 * @return Return value description
 *
 * @code
 * Example usage code
 * @endcode
 *
 * @see RelatedClass
 */
void function(int param_name);
```

### Markdown Files

- Use ATX-style headers (`#` not underlines)
- Code blocks with language hints
- Link to other documentation
- Include examples

### Test Documentation

Each test should document:
- **What it tests** - Feature/behavior being validated
- **Expected behavior** - Step-by-step execution
- **What it validates** - Specific assertions
- **Why it's important** - Context and motivation

## Updating Documentation

### When Code Changes

1. **New feature** → Update USER_GUIDE.md
2. **New test** → Update TEST_DESCRIPTIONS.md
3. **API change** → Update code comments (Doxygen)
4. **Bug fix** → Add to test documentation

### Rebuild Documentation

```bash
./docs/build_docs.sh
```

### Check Documentation

```bash
# Check Doxygen warnings
doxygen Doxyfile 2>&1 | grep -i warning

# Check Sphinx warnings
cd sphinx
make html 2>&1 | grep -i warning
```

## Documentation Checklist

Before committing documentation changes:

- [ ] All links work (no 404s)
- [ ] Code examples tested
- [ ] Screenshots up-to-date
- [ ] Doxygen builds without warnings
- [ ] Sphinx builds without warnings
- [ ] New features documented
- [ ] Test descriptions updated

## Contributing Documentation

### Adding New Documentation

1. Create Markdown file in `docs/`
2. Add link to relevant index files
3. Follow existing format and style
4. Include examples
5. Test build: `./build_docs.sh`

### Improving Existing Documentation

1. Fix errors or outdated information
2. Add missing examples
3. Clarify confusing sections
4. Add cross-references
5. Test build: `./build_docs.sh`

## Documentation TODOs

### Needed Documentation

- [ ] Architecture overview diagram
- [ ] Performance tuning guide
- [ ] Large-scale simulation guide
- [ ] Python bindings tutorial (when available)
- [ ] Contribution guidelines
- [ ] Release notes

### Documentation Improvements

- [ ] More code examples
- [ ] Video tutorials
- [ ] Interactive examples
- [ ] FAQ section
- [ ] Glossary of terms

## Documentation Tools

### Doxygen

- **Purpose**: C++ API documentation
- **Input**: C++ source files with Doxygen comments
- **Output**: HTML, LaTeX
- **Config**: `Doxyfile`

### Sphinx

- **Purpose**: General/Python documentation
- **Input**: RST and Markdown files
- **Output**: HTML, PDF, EPUB
- **Config**: `sphinx/conf.py`

### Markdown

- **Purpose**: Simple text documentation
- **Input**: `.md` files
- **Output**: Rendered on GitHub, converted by Sphinx
- **Standard**: CommonMark with GFM extensions

## Online Documentation

Once published, documentation will be available at:
- **GitHub Pages**: `https://username.github.io/dr_evt/`
- **Read the Docs**: `https://dr-evt.readthedocs.io/`

## Support

For documentation issues:
1. Check this README
2. Review existing documentation
3. File issue with documentation feedback

## License

Documentation is licensed under MIT License, same as source code.
