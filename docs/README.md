---
orphan: true
---

# Documentation Development

The published documentation is organized by the toctrees in
[`index.md`](index.md). Add user documentation to the narrowest relevant
page and link to that page elsewhere instead of copying its contents.
The Quick Start is intentionally self-contained and may repeat essential setup
and first-run instructions.

## Build locally

Create an isolated environment and install the documentation dependencies:

```bash
python3 -m venv .venv-docs
source .venv-docs/bin/activate
python -m pip install -r docs/requirements.txt
make -C docs html
```

The HTML output is written to `docs/_build/html`. If Doxygen is available,
the build also generates the C++ Development API Reference; otherwise that optional
portion is skipped.

Read the Docs deployment configuration is documented in
[ReadTheDocs Setup](dev/READTHEDOCS_SETUP.md).

## Authoring rules

- Use ATX headings and fenced code blocks with language identifiers.
- Use relative links between repository documentation pages.
- Keep each fact in one authoritative page; use short contextual links from
  other pages.
- Keep examples runnable and update tests that validate documented commands.
- Add new pages to the appropriate toctree in [`index.md`](index.md).
