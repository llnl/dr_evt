---
orphan: true
---

# DR_EVT Documentation

Complete documentation for the DR_EVT HPC Job Scheduler Simulator.

## Documentation Structure

```
docs/
├── index.md                       # Main documentation index
├── getting-started/                # New user guides
│   ├── quickstart.md              # 5-minute quick start
│   ├── installation.md            # Build and install
│   └── tutorial.md                # Step-by-step tutorial
├── user-guide/                     # Complete user manual
│   ├── overview.md                # User guide overview
│   ├── command-line.md            # All CLI options
│   ├── trace-formats.md           # Input trace file formats
│   ├── protobuf-config.md         # Protobuf config file format
│   ├── grpc-setup.md              # Client/server build/setup
│   └── client-server-use-cases.md # Client/server deployment examples
├── dev/                             # Developer documentation
│   ├── README.md                  # Developer docs index
│   ├── BLOCK_WAIT_QUEUE.md        # Block wait-queue implementation and tests
│   ├── READTHEDOCS_SETUP.md       # ReadTheDocs configuration notes
│   └── design-decisions/          # Why we made choices
│       ├── README.md
│       ├── BLOCK_QUEUE.md
│       ├── BLOCK_QUEUE_TESTING.md
│       ├── CIRCULAR_QUEUE.md
│       ├── SIMULATION_VS_REPLAY_MODES.md
│       ├── TIMEZONE_SUPPORT.md
│       └── OUT_TRACE_STREAMING.md
├── reference/                      # Technical reference
│   └── terminology.md             # Terms and definitions
├── api/                             # API documentation
│   ├── PYTHON_API.md              # Python bindings reference
│   └── STREAMING_API.md           # Streaming/online API guide
├── BACKFILLING_ALGORITHMS.md       # EASY and Conservative backfilling
├── CLIENT_SERVER_GUIDE.md         # gRPC client/server guide
└── TESTING_GUIDE.md               # Test suite documentation
```

## Quick Navigation

### For New Users
1. [Quick Start](getting-started/quickstart.md) - Get running in 5 minutes
2. [Installation](getting-started/installation.md) - Build from source
3. [Tutorial](getting-started/tutorial.md) - Your first simulation

### For Regular Users
1. [User Guide](user-guide/overview.md) - Complete manual
2. [Command-Line Options](user-guide/command-line.md) - All options
3. [Trace Formats](user-guide/trace-formats.md) - Input files
4. [Client/Server Setup](user-guide/grpc-setup.md) - Network client/server setup
5. [Client/Server Use Cases](user-guide/client-server-use-cases.md) - Bare-metal, container, and multi-server deployment patterns

### For Researchers
1. [Test Summary](TESTING_GUIDE.md#test-summary) - ✓ All tests pass
2. [Backfilling Algorithms](BACKFILLING_ALGORITHMS.md) - EASY and Conservative
3. [Scheduler Correctness Tests](TESTING_GUIDE.md#scheduler-correctness-tests) - Python-reference ground truth comparison

### For Developers
1. [Backfilling Algorithms](BACKFILLING_ALGORITHMS.md) - How it works
2. [Design Decisions](dev/design-decisions/README.md) - Why, and system design notes
3. [Streaming API](api/STREAMING_API.md) - Online simulation API

## Building Documentation

### Prerequisites

```bash
# From the repository root: create an isolated environment for documentation
# tooling, then activate it before installing the Sphinx dependencies.
python3 -m venv .venv-docs
source .venv-docs/bin/activate
python -m pip install -r docs/requirements.txt
```

Using a virtual environment is recommended so Sphinx and its extensions do
not modify the system Python installation. `make install` in `docs/` installs
into the currently active Python environment; it does not create one.

Installs:
- Sphinx (documentation generator)
- sphinx-rtd-theme (ReadTheDocs theme)
- myst-parser (Markdown support)

Install Doxygen separately through your system package manager (for example,
`apt install doxygen` or `brew install doxygen`) to generate the optional C++
API reference. The Python dependencies include Breathe, which renders
Doxygen's XML in the Sphinx site using the same theme. Doxygen itself is not
required for the rest of the Sphinx documentation site.

### HTML site and C++ API reference

```bash
source .venv-docs/bin/activate
make -C docs html
```

`make html` builds the Sphinx site. When Doxygen is available, it first
generates XML and Breathe renders the C++ API inside the Sphinx site using the
same theme and navigation; otherwise it skips that optional reference. Read
the Docs follows the same sequence. Run `make doxygen` when only refreshing
the API XML is needed.

### Build HTML Documentation

```bash
source .venv-docs/bin/activate
sphinx-build -b html docs docs/_build/html
```

View at: `docs/_build/html/index.html`

### Build PDF

```bash
sphinx-build -b latex . _build/latex
cd _build/latex
make
```

Output: `DR_EVT.pdf`

### Auto-rebuild on Changes

```bash
pip install sphinx-autobuild
sphinx-autobuild . _build/html
```

Opens browser with live reload at `http://localhost:8000`

## ReadTheDocs

Documentation is automatically built and published at:
https://dr-evt.readthedocs.io/

Configuration: `.readthedocs.yaml`

## Documentation Standards

### Markdown Format

- Use ATX headers (`#` not underlines)
- Code blocks with language hints:
  ````markdown
  ```bash
  ./simulator trace.csv
  ```
  ````
- Link to other docs with relative paths:
  ```markdown
  [User Guide](user-guide/overview.md)
  ```

### Structure

Each major section should have:
1. **Overview** - What this section covers
2. **Examples** - Concrete code examples
3. **Reference** - Technical details
4. **See Also** - Links to related docs

### Code Examples

All code examples should:
- Be runnable (test them!)
- Include expected output
- Explain what's happening

### Updates

When code changes:
1. **New feature** → Update user guide + add example
2. **New test** → Update TESTING_GUIDE.md
3. **API change** → Update PYTHON_API.md or STREAMING_API.md
4. **Bug fix** → Update dev/design-decisions/ if it reflects a design choice

## Contributing

To add/improve documentation:

1. Follow existing structure and style
2. Add to appropriate section
3. Update navigation/index files
4. Test build locally: `sphinx-build -b html . _build/html`
5. Submit pull request

## Documentation Checklist

Before committing:
- [ ] All links work (no 404s)
- [ ] Code examples tested
- [ ] Builds without warnings
- [ ] Added to navigation/index
- [ ] Follows style guide

## Support

For documentation issues:
- File issue: https://github.com/LLNL/dr_evt/issues
- Tag with: `documentation`

## License

Documentation licensed under MIT License, same as source code.
