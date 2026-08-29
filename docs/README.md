# DR_EVT Documentation

Complete documentation for the DR_EVT HPC Job Scheduler Simulator.

## Documentation Structure

```
docs/
├── index.md                    # Main documentation index
├── getting-started/            # New user guides
│   ├── quickstart.md          # 5-minute quick start
│   ├── installation.md        # Build and install
│   └── tutorial.md            # Step-by-step tutorial
├── user-guide/                 # Complete user manual
│   ├── overview.md            # User guide overview
│   ├── command-line.md        # All CLI options
│   ├── trace-formats.md       # Input file formats
│   ├── scheduling-policies.md # Algorithm explanations
│   └── simulation-modes.md    # Replay vs simulation
├── verification/               # Correctness verification
│   ├── summary.md             # ✓ 23/23 tests pass
│   ├── analytical.md          # Hand-traced verification
│   ├── easy-backfilling.md    # Algorithm properties
│   └── test-descriptions.md   # Test explanations
├── development/                # Developer documentation
│   ├── algorithm.md           # Simulation algorithm
│   ├── architecture.md        # System design
│   ├── design-decisions/      # Why we made choices
│   └── contributing.md        # How to contribute
├── reference/                  # Technical reference
│   ├── terminology.md         # Terms and definitions
│   └── config-files.md        # Configuration formats
└── api/                        # API documentation
    └── index.md               # C++ API reference
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

### For Researchers
1. [Verification Summary](verification/summary.md) - ✓ All tests pass
2. [EASY Backfilling](verification/easy-backfilling.md) - Algorithm
3. [Analytical Verification](verification/analytical.md) - Ground truth

### For Developers
1. [Algorithm](development/algorithm.md) - How it works
2. [Architecture](development/architecture.md) - System design
3. [Design Decisions](development/design-decisions.md) - Why

## Building Documentation

### Prerequisites

```bash
pip install -r requirements.txt
```

Installs:
- Sphinx (documentation generator)
- sphinx-rtd-theme (ReadTheDocs theme)
- myst-parser (Markdown support)

### Build HTML Documentation

```bash
cd docs
sphinx-build -b html . _build/html
```

View at: `_build/html/index.html`

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
2. **New test** → Update verification docs
3. **API change** → Update API reference
4. **Bug fix** → Update development notes

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
