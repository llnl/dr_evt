# ReadTheDocs Setup Guide

## Overview

DR_EVT documentation is now hosted on ReadTheDocs at:
**https://dr-evt.readthedocs.io/**

The documentation is automatically built from the `main` branch using Sphinx.

## Configuration Files

### `.readthedocs.yaml` (Repository Root)

Main configuration file for ReadTheDocs builds:
- **Build OS**: Ubuntu 22.04
- **Python version**: 3.11
- **Sphinx config**: `docs/conf.py`
- **Requirements**: `docs/requirements.txt`
- **Output formats**: HTML (web), PDF, EPUB

### `docs/conf.py`

Sphinx configuration:
- **Theme**: `sphinx_rtd_theme` (ReadTheDocs theme)
- **Extensions**:
  - `myst_parser` - Markdown support
  - `sphinx.ext.autodoc` - API documentation
  - `sphinx.ext.napoleon` - Google/NumPy docstring support
  - `sphinx.ext.viewcode` - Source code links
  - `sphinx.ext.githubpages` - GitHub Pages support
- **Master document**: `docs/index.md`
- **Supported formats**: Markdown (`.md`), reStructuredText (`.rst`)

### `docs/requirements.txt`

Python dependencies for documentation builds:
```
sphinx>=5.0
sphinx-rtd-theme>=1.2.0
myst-parser>=1.0.0
```

## Documentation Structure

```
docs/
├── index.md                          # Main landing page with toctree
├── conf.py                          # Sphinx configuration
├── requirements.txt                 # Python dependencies
├── Makefile                         # Local build commands
├── _static/                         # Custom CSS/JS (tracked via .gitkeep)
├── _build/                          # Build output (ignored in .gitignore)
├── getting-started/
│   ├── quickstart.md
│   ├── installation.md
│   └── tutorial.md
├── user-guide/
│   ├── overview.md                # User guide overview
│   ├── command-line.md            # All CLI options
│   ├── trace-formats.md           # Input trace file formats
│   ├── protobuf-config.md         # Protobuf config file format
│   └── grpc-setup.md              # gRPC build/setup
├── reference/
│   └── terminology.md
├── dev/                             # Special .gitignore handling (see below)
│   ├── README.md                    # Tracked: Dev documentation index
│   ├── design-decisions/            # Tracked: Architectural decisions
│   │   ├── README.md
│   │   ├── SIMULATION_VS_REPLAY_MODES.md
│   │   └── TIMEZONE_SUPPORT.md
│   └── session-notes/               # Ignored: Personal notes (local only)
├── EASY_BACKFILLING_ALGORITHM.md
├── TESTING_GUIDE.md
├── STREAMING_API.md
├── PYTHON_API.md
├── CLI_OPTIONS.md
├── DOCUMENTATION_ORGANIZATION.md
└── READTHEDOCS_SETUP.md
```

## Building Locally

Test documentation builds before pushing:

```bash
cd docs

# Install dependencies (one-time)
pip install -r requirements.txt

# Build HTML
make html
# Output: docs/_build/html/index.html

# Build PDF (requires LaTeX)
make pdf
# Output: docs/_build/latex/DR_EVT.pdf

# Serve with live-reload
make serve
# Opens browser at http://localhost:8000

# Check for broken links
make linkcheck
```

## ReadTheDocs Project Setup

### Initial Setup (One-Time)

1. **Import project on ReadTheDocs**:
   - Go to https://readthedocs.org/dashboard/import/
   - Connect GitHub account if not already connected
   - Select `LLNL/dr_evt` repository
   - Click "Import"

2. **Configure project**:
   - Project name: `dr-evt`
   - Programming language: `Python`
   - Documentation type: `Sphinx`
   - Default branch: `main`
   - Privacy level: `Public`

3. **Build settings** (automatically detected from `.readthedocs.yaml`):
   - Configuration file: `.readthedocs.yaml`
   - Requirements file: `docs/requirements.txt`
   - Python version: 3.11

4. **Trigger first build**:
   - Click "Build version" button
   - Wait for build to complete
   - Check build logs if errors occur

### Webhook (Automatic)

ReadTheDocs automatically sets up a GitHub webhook for automatic builds:
- **Trigger**: Push to `main` branch
- **Action**: Rebuild documentation
- **Delay**: Usually 1-2 minutes after push

### Build Status

Check build status:
- **Dashboard**: https://readthedocs.org/projects/dr-evt/
- **Build history**: https://readthedocs.org/projects/dr-evt/builds/
- **Latest docs**: https://dr-evt.readthedocs.io/en/latest/

### Badges

Add badges to README.md:
```markdown
[![Documentation Status](https://readthedocs.org/projects/dr-evt/badge/?version=latest)](https://dr-evt.readthedocs.io/en/latest/?badge=latest)
```

## Version Management

### Multiple Versions

ReadTheDocs can host multiple versions:
- **latest**: Built from `main` branch (default)
- **stable**: Built from latest Git tag
- **v1.0**: Built from `v1.0` tag

To enable version tags:
1. Tag a release: `git tag -a v1.0.0 -m "Release 1.0.0"`
2. Push tags: `git push origin --tags`
3. ReadTheDocs automatically builds tagged versions

### Activating Versions

In ReadTheDocs dashboard:
1. Go to **Versions** tab
2. Select versions to activate
3. Set default version (usually `latest` or `stable`)

## Customization

### Theme Options (`docs/conf.py`)

```python
html_theme_options = {
    'navigation_depth': 4,        # How deep to show in sidebar
    'collapse_navigation': False,  # Keep navigation expanded
    'sticky_navigation': True,     # Sticky sidebar
    'includehidden': True,         # Include hidden toctrees
    'titles_only': False,          # Show all headers in toc
}
```

### Custom CSS/JS

Add custom styling:
1. Create `docs/_static/custom.css`
2. Update `conf.py`:
   ```python
   html_static_path = ['_static']
   html_css_files = ['custom.css']
   ```

### Logo and Favicon

Add project branding:
```python
html_logo = '_static/logo.png'
html_favicon = '_static/favicon.ico'
```

## Troubleshooting

### Build Failures

Check build logs on ReadTheDocs:
1. Go to project dashboard
2. Click "Builds" tab
3. Click on failed build
4. Review error messages

Common issues:
- **Missing requirements**: Add to `docs/requirements.txt`
- **Sphinx errors**: Check `docs/conf.py` syntax
- **Broken links**: Run `make linkcheck` locally
- **Missing files**: Ensure all referenced files exist in repo

### Warning: Broken Cross-References

These warnings are non-fatal but should be fixed:
```
WARNING: 'myst' cross-reference target not found: 'trace-formats.md'
```

Fix by:
1. Ensuring target file exists
2. Using correct relative paths
3. Adding file to toctree in `index.md`

### Local Build vs ReadTheDocs

If build works locally but fails on ReadTheDocs:
- Check Python version matches (3.11)
- Verify all dependencies in `requirements.txt`
- Check file paths are relative, not absolute
- Test in clean virtual environment

## Maintenance

### Regular Tasks

- **Update copyright year** in `conf.py`
- **Fix broken links** with `make linkcheck`
- **Update version numbers** when releasing
- **Review build logs** after major changes

### Documentation Updates

All changes to `docs/` directory automatically trigger rebuilds:
1. Edit Markdown files in `docs/`
2. Commit and push to `main`
3. ReadTheDocs webhook triggers build (1-2 min delay)
4. Check build status and published docs

## Migration Notes

### Changes Made

1. **Created** `.readthedocs.yaml` - ReadTheDocs configuration
2. **Updated** `docs/index.md` - Added toctree directives and badges
3. **Updated** `docs/conf.py` - Added metadata for ReadTheDocs
4. **Updated** `README.md` - Added ReadTheDocs badge and primary documentation link
5. **Removed** `docs/sphinx/` - Redundant empty subdirectory

### No Breaking Changes

- All Markdown files remain in place
- Documentation still readable on GitHub
- Local Sphinx builds work identically
- No changes to documentation content

## References

- [ReadTheDocs Documentation](https://docs.readthedocs.io/)
- [Sphinx Documentation](https://www.sphinx-doc.org/)
- [MyST Parser (Markdown in Sphinx)](https://myst-parser.readthedocs.io/)
- [Sphinx RTD Theme](https://sphinx-rtd-theme.readthedocs.io/)
