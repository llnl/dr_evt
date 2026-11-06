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

Use Python 3.11 or newer with the supported Sphinx 8.x and Breathe 5.1+
toolchain. Create an isolated environment and install the dependencies:

```bash
python3 -m venv .venv-docs
source .venv-docs/bin/activate
python -m pip install -r docs/requirements.txt
make -C docs html
```

The HTML output is written to `docs/_build/html`. If Doxygen is available,
the build also generates the C++ Development API Reference; otherwise that optional
portion is skipped.

Environments created with an older Python cannot install the supported
toolchain. Recreate or upgrade the documentation environment before installing
`docs/requirements.txt`; older Sphinx/Breathe versions can emit duplicate
declaration and namespace-target warnings.

Read the Docs deployment configuration is documented in
[ReadTheDocs Setup](dev/READTHEDOCS_SETUP.md).

## Authoring rules

- Use ATX headings and fenced code blocks with language identifiers.
- Use relative links between repository documentation pages.
- Keep each fact in one authoritative page; use short contextual links from
  other pages.
- Keep examples runnable and update tests that validate documented commands.
- Add new pages to the appropriate toctree in [`index.md`](index.md).
- Wrap Breathe directives and navigation to generated source pages in
  `if-doxygen` blocks. Unlike Sphinx's `only` directive, these skip parsing when
  Doxygen XML is absent, so documentation-only builds remain valid.
