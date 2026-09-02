# Documentation Organization

> **Status note (added after discovering this caused real problems):** the
> "New Structure" below was a *plan*, and its own "Next Steps" section
> admits several pages (`trace-formats.md` etc.) were still to be created.
> Most of `verification/` and much of `development/` described here were
> never actually written - `docs/index.md` linked to this plan's paths as
> if they existed, producing dead links across roughly two-thirds of its
> navigation. If you're using this file to understand the *current*
> structure, check the file actually exists first; if you're using it to
> finish the reorganization, treat it as a to-do list, not a completed
> migration record.

Complete reorganization of DR_EVT documentation into ReadTheDocs-style structure.

## New Structure

```
docs/
├── index.md                          # Main entry point
├── conf.py                           # Sphinx configuration
├── requirements.txt                  # Documentation build dependencies
├── Makefile                          # Build commands
├── README.md                         # Documentation guide
│
├── getting-started/                  # New user onboarding
│   ├── quickstart.md                # 5-minute quick start
│   ├── installation.md              # Build and install guide
│   └── tutorial.md                  # Step-by-step first simulation
│
├── user-guide/                       # Complete user manual
│   ├── overview.md                  # User guide overview (was USER_GUIDE.md)
│   ├── command-line.md              # All CLI options
│   ├── trace-formats.md             # Input file format specs
│   ├── scheduling-policies.md       # Algorithm explanations
│   └── simulation-modes.md          # Replay vs simulation
│
├── verification/                     # Correctness and testing
│   ├── summary.md                   # ✓ 23/23 tests pass (was VERIFICATION_COMPLETE.md)
│   ├── analytical.md                # Hand-traced verification (was ANALYTICAL_VERIFICATION_PLAN.md)
│   ├── easy-backfilling.md          # Algorithm properties (was EASY_BACKFILLING_PROPERTIES.md)
│   ├── test-descriptions.md         # What each test validates (was TEST_DESCRIPTIONS.md)
│   ├── correctness-tests.md         # Correctness tests (was BACKFILL_CORRECTNESS_TESTS.md)
│   ├── tests-explained.md           # Test explanations (was BACKFILL_TESTS_EXPLAINED.md)
│   ├── minimal-tests.md             # Minimal tests (was MINIMAL_CORRECTNESS_TESTS.md)
│   ├── invariant-tests.md           # Invariants (was INVARIANT_TESTS.md)
│   ├── oracle.md                    # Oracle verification (was ORACLE_VERIFICATION.md)
│   ├── timing-decision.md           # Timing rules (was BACKFILL_TIMING_DECISION.md)
│   └── implementation.md            # Implementation correctness (was IMPLEMENTATION_CORRECTNESS.md)
│
├── development/                      # Developer documentation
│   ├── algorithm.md                 # Simulation algorithm (was SIMULATION_ALGORITHM.md)
│   ├── simulation-logic.md          # Simulation logic (was SIMULATION_LOGIC.md)
│   ├── scheduler-optimization.md    # Optimizations (was SCHEDULER_OPTIMIZATION.md)
│   ├── bug-fixes.md                 # Bug fix summary (was BUG_FIX_SUMMARY.md)
│   ├── bug-status.md                # Bug status (was BUG_STATUS.md)
│   ├── test-0-replay.md             # Test 0 replay (was TEST_0_REPLAY_ACCOUNTING.md)
│   ├── test-0-scheduler.md          # Test 0 scheduler (was TEST_0_SCHEDULER_CORRECTNESS.md)
│   ├── architecture.md              # System architecture (NEW)
│   ├── contributing.md              # Contribution guide (NEW)
│   └── design-decisions/            # Design decision documents
│       ├── CONAN_REMOVAL.md
│       ├── RENAMING_SUMMARY.md
│       ├── SIMULATION_VS_REPLAY_MODES.md
│       ├── TIMEZONE_SUPPORT.md
│       └── TRACE_FORMAT.md
│
├── reference/                        # Technical reference
│   ├── terminology.md               # Terms and definitions (was TERMINOLOGY.md)
│   └── config-files.md              # Configuration formats (NEW)
│
├── api/                              # API documentation
│   └── index.md                     # C++ API reference (NEW)
│
└── dev/                              # Development notes
    └── design-decisions/            # Design documents
```

### Install Dependencies for Sphinx documents

```bash
cd docs
pip install -r requirements.txt
```

### Build HTML

```bash
make html
```

Output: `_build/html/index.html`

### Build PDF

```bash
make pdf
```

Output: `_build/latex/DR_EVT.pdf`

### Live Preview

```bash
make serve
```

Opens browser at `http://localhost:8000` with auto-reload.

### Check Links

```bash
make linkcheck
```

Verifies all internal and external links.

## ReadTheDocs Integration

### Configuration
- `.readthedocs.yaml` - ReadTheDocs build configuration
- `docs/conf.py` - Sphinx configuration
- `docs/requirements.txt` - Python dependencies

### Publishing
Once connected to ReadTheDocs:
1. Push to GitHub
2. ReadTheDocs automatically builds
3. Published at: `https://dr-evt.readthedocs.io/`

### Features
- ✓ Automatic builds on git push
- ✓ Version management (latest, stable, tags)
- ✓ PDF/EPUB downloads
- ✓ Search functionality
- ✓ Mobile-friendly

## Navigation Flow

### New User Journey
1. [Main Index](index.md) → Overview
2. [Quick Start](getting-started/quickstart.md) → Run in 5 minutes
3. [Tutorial](getting-started/tutorial.md) → First simulation
4. [User Guide](user-guide/overview.md) → Complete reference

### Researcher Journey
1. [Main Index](index.md) → Overview
2. [Verification Summary](verification/summary.md) → ✓ All tests pass
3. [EASY Backfilling](verification/easy-backfilling.md) → Algorithm
4. [Analytical Verification](verification/analytical.md) → Ground truth

### Developer Journey
1. [Main Index](index.md) → Overview
2. [Algorithm](development/algorithm.md) → How it works
3. [Architecture](development/architecture.md) → System design
4. [Design Decisions](development/design-decisions.md) → Why

## Documentation Principles

### Organization
- **Audience-focused** - Content organized by user type
- **Progressive disclosure** - Simple → detailed
- **Clear navigation** - Each page knows where it fits

### Content
- **Examples first** - Show, then explain
- **Runnable code** - All examples are tested
- **Cross-references** - Link related concepts

### Maintenance
- **One source of truth** - No duplicate information
- **Update on change** - Code changes → doc updates
- **Version control** - Track doc changes with code

## Migration Guide

### For Users
- Old `QUICKSTART.md` → `getting-started/quickstart.md`
- Old `USER_GUIDE.md` → `user-guide/overview.md`
- Old `TEST_DESCRIPTIONS.md` → `verification/test-descriptions.md`

### For Developers
- Old `SIMULATION_ALGORITHM.md` → `development/algorithm.md`
- Old `BUG_FIX_SUMMARY.md` → `development/bug-fixes.md`
- Design docs still in `dev/design-decisions/`

### For Links
All old doc links need updating:
- Search: `docs/OLD_NAME.md`
- Replace: `docs/section/new-name.md`

## Benefits

1. **Professional appearance** - ReadTheDocs style
2. **Better navigation** - Clear structure
3. **Easier maintenance** - Logical organization
4. **Better discoverability** - Proper categorization
5. **Multiple formats** - HTML, PDF, EPUB
6. **Search functionality** - Sphinx search
7. **Version management** - Via ReadTheDocs

## Next Steps

1. **Fill gaps** - Create placeholder pages (trace-formats.md, etc.)
2. **Update cross-references** - Fix all internal links
3. **Add diagrams** - Architecture and algorithm diagrams
4. **API documentation** - Generate from C++ comments
5. **Connect ReadTheDocs** - Set up automatic builds
6. **Add examples** - More code examples throughout
7. **Create videos** - Tutorial screencasts (optional)

## Feedback

For documentation feedback or improvements:
- File issue: https://github.com/LLNL/dr_evt/issues
- Tag: `documentation`
- Reference this document
