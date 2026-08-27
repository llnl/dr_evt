# DR_EVT Documentation Index

Complete documentation for DR_EVT HPC Job Scheduler Simulator.

## 📚 Quick Links

| Document | Purpose | Audience |
|----------|---------|----------|
| [User Guide](docs/USER_GUIDE.md) | Complete usage manual | End users |
| [Quick Start](QUICKSTART.md) | Fast reference | Everyone |
| [Testing Guide](TESTING.md) | Running tests | Testers/developers |
| [Test Descriptions](docs/TEST_DESCRIPTIONS.md) | What each test checks | Testers |
| [API Docs](docs/api/html/index.html) | C++ API reference | Developers |

## For End Users

### Getting Started
1. **[User Guide](docs/USER_GUIDE.md)** - Complete manual covering:
   - Installation and setup
   - Command-line options
   - Trace file formats (Simple and Lassen)
   - Scheduler policies (EASY, Conservative, FCFS, SJF, LJF)
   - Timestamp formats (Epoch, ISO)
   - Troubleshooting guide
   - Best practices

2. **[Quick Start](QUICKSTART.md)** - Fast reference:
   - Basic commands
   - Common workflows
   - Quick examples

3. **[Trace Format Specification](TRACE_FORMAT.md)** - Creating traces:
   - Simple format (7 columns)
   - Lassen format (33 columns)
   - Timestamp formats
   - Column descriptions
   - Usage examples

### Running Simulations

```bash
# Basic simulation
./simulator trace.csv --total_nodes 100 --backfill_policy easy

# With all options
./simulator trace.csv \
  --trace_format simple \
  --timestamp_format epoch \
  --total_nodes 100 \
  --backfill_policy easy \
  --priority_policy fcfs \
  --runtime_mode actual
```

## For Testers

### Testing Documentation
1. **[Testing Guide](TESTING.md)** - Complete testing guide:
   - Running test suites (bash and Python)
   - Understanding test results
   - Adding new tests
   - CI/CD integration

2. **[Test Descriptions](docs/TEST_DESCRIPTIONS.md)** - Detailed explanations:
   - What each test validates
   - Expected behavior
   - Why tests are important
   - How to interpret failures

3. **[Test Results](TEST_SUMMARY.md)** - Validation results:
   - Comprehensive test campaign
   - 15 tests, 100% passing
   - Performance metrics
   - Coverage analysis

### Running Tests

```bash
# Bash test suite (fast, 8 tests)
./tests/run_tests.sh

# Python test suite (detailed, 7 tests)
python3 tests/test_scheduler.py

# Both
./tests/run_tests.sh && python3 tests/test_scheduler.py
```

**Expected**: All 15 tests pass in ~3 seconds

## For Developers

### Development Documentation
1. **[API Documentation](docs/api/html/index.html)** - Doxygen-generated:
   - Class hierarchies
   - Function references
   - Code examples
   - Call graphs

2. **[Build Setup](BUILD_SETUP.md)** - Building from source:
   - Dependencies (Protobuf, Boost)
   - CMake configuration
   - Build instructions
   - Platform-specific notes

3. **[Implementation Status](TEST_STATUS.md)** - Current state:
   - Completed features
   - Known limitations
   - Future work

### Code Documentation Standards

All C++ code uses Doxygen format:
```cpp
/**
 * @brief One-line description
 *
 * Detailed description with algorithm explanation.
 *
 * @param param Description
 * @return Return value
 *
 * @code
 * Example usage
 * @endcode
 */
```

### Building Documentation

```bash
# Install tools
brew install doxygen graphviz  # macOS
pip3 install sphinx sphinx-rtd-theme myst-parser

# Build all documentation
cd docs && ./build_docs.sh

# View
open docs/api/html/index.html
open docs/sphinx/_build/html/index.html
```

## For Researchers

### Algorithm Documentation
1. **[User Guide - Scheduler Policies](docs/USER_GUIDE.md#scheduler-policies)**
   - EASY vs Conservative backfill
   - Priority policies (FCFS, SJF, LJF)
   - Runtime estimation modes

2. **[Backfill Verification](BACKFILL_VERIFICATION.md)** - Algorithm analysis:
   - Success and failure cases
   - Resource utilization
   - Idle resource scenarios
   - Performance characteristics

3. **[Saturation Test Results](SATURATION_TEST_RESULTS.md)** - Stress analysis:
   - 30-job complex workload
   - Resource contention handling
   - Performance under load

### Research Use Cases

**Policy Comparison**:
```bash
# Compare backfill policies
./simulator trace.csv --backfill_policy easy -o easy.txt
./simulator trace.csv --backfill_policy conservative -o conservative.txt
diff easy.txt conservative.txt
```

**Priority Studies**:
```bash
# Compare priority policies
./simulator trace.csv --priority_policy fcfs -o fcfs.txt
./simulator trace.csv --priority_policy sjf -o sjf.txt
./simulator trace.csv --priority_policy ljf -o ljf.txt
```

## Documentation Structure

```
DR_EVT/
├── DOCUMENTATION.md           # This file (index)
├── QUICKSTART.md             # Quick reference
├── TESTING.md                # Testing guide
├── TEST_SUMMARY.md           # Test results
├── BACKFILL_VERIFICATION.md  # Backfill analysis
├── SATURATION_TEST_RESULTS.md # Stress test
├── TRACE_FORMAT.md           # Trace formats
├── BUILD_SETUP.md            # Build instructions
├── TEST_STATUS.md            # Implementation status
├── docs/
│   ├── USER_GUIDE.md         # Complete user manual
│   ├── TEST_DESCRIPTIONS.md  # Test explanations
│   ├── README.md             # Docs overview
│   ├── Doxyfile              # Doxygen config
│   ├── build_docs.sh         # Build script
│   ├── api/                  # Generated API docs
│   └── sphinx/               # Sphinx docs
│       ├── conf.py           # Sphinx config
│       └── index.rst         # Sphinx index
├── tests/
│   ├── README.md             # Test suite docs
│   ├── run_tests.sh          # Bash tests
│   └── test_scheduler.py     # Python tests
└── test_traces/              # Test data
    ├── epoch_pbatch.csv      # Basic test
    ├── saturation_test.csv   # Stress test
    └── ...
```

## Documentation by Topic

### Installation & Setup
- [Build Setup](BUILD_SETUP.md)
- [Quick Start](QUICKSTART.md)

### Usage & Configuration
- [User Guide](docs/USER_GUIDE.md)
- [Trace Format Specification](TRACE_FORMAT.md)

### Testing & Validation
- [Testing Guide](TESTING.md)
- [Test Descriptions](docs/TEST_DESCRIPTIONS.md)
- [Test Results](TEST_SUMMARY.md)

### Algorithm Analysis
- [Backfill Verification](BACKFILL_VERIFICATION.md)
- [Saturation Test Results](SATURATION_TEST_RESULTS.md)

### Development
- [API Documentation](docs/api/html/index.html) (generated)
- [Build Setup](BUILD_SETUP.md)
- [Implementation Status](TEST_STATUS.md)

## Documentation Formats

### Markdown (`.md`)
- Human-readable source
- Rendered on GitHub
- Used for most documentation

### ReStructuredText (`.rst`)
- Sphinx native format
- Used for API index

### HTML (Generated)
- Doxygen output (C++ API)
- Sphinx output (general docs)
- View in browser

## Documentation Coverage

| Area | Coverage | Status |
|------|----------|--------|
| User guide | 100% | ✅ Complete |
| Quick start | 100% | ✅ Complete |
| Testing | 100% | ✅ Complete |
| Test descriptions | 100% | ✅ Complete |
| API reference | 80% | ✅ Good |
| Architecture | 50% | ⚠️ Partial |
| Examples | 100% | ✅ Complete |
| Troubleshooting | 80% | ✅ Good |

## Getting Help

### By Topic

**Installation issues** → [Build Setup](BUILD_SETUP.md)

**Usage questions** → [User Guide](docs/USER_GUIDE.md)

**Test failures** → [Test Descriptions](docs/TEST_DESCRIPTIONS.md)

**API questions** → [API Documentation](docs/api/html/index.html)

**Performance issues** → [Saturation Test Results](SATURATION_TEST_RESULTS.md)

### Documentation Issues

If documentation is unclear or incorrect:
1. Check if there's an update in another doc
2. File issue with details about what's confusing
3. Suggest improvements

## Contributing Documentation

### Adding Documentation
1. Create Markdown file in appropriate location
2. Follow existing format and style
3. Add to this index
4. Include examples
5. Test build: `cd docs && ./build_docs.sh`

### Updating Documentation
1. Fix errors or outdated info
2. Add missing examples
3. Clarify confusing sections
4. Add cross-references
5. Test build

## Documentation TODOs

### High Priority
- [ ] Architecture overview diagram
- [ ] Performance tuning guide
- [ ] Python bindings tutorial (when available)

### Medium Priority
- [ ] Video tutorials
- [ ] FAQ section
- [ ] Contribution guidelines
- [ ] Release notes template

### Low Priority
- [ ] Interactive examples
- [ ] Glossary of terms
- [ ] PDF export of documentation

## Version History

- **v1.0.0** (2024): Initial release
  - Complete user guide
  - Comprehensive test suite
  - API documentation (Doxygen)
  - Test descriptions

## License

Documentation is licensed under MIT License, same as source code.

---

**Last Updated**: 2024
**Status**: Complete and validated
**Maintainer**: DR_EVT Development Team
