# Read the Docs Deployment

DR_EVT documentation is published at
[dr-evt.readthedocs.io](https://dr-evt.readthedocs.io/). Read the Docs builds
the `main` branch with Sphinx and publishes it as the `latest` version.

For local builds and authoring conventions, see
[Documentation Development](../README.md).

## Configuration

The repository-root `.readthedocs.yaml` selects the build environment, Python
version, Sphinx configuration, dependency file, and output formats. Sphinx
settings are in `docs/conf.py`, and Python dependencies are in
`docs/requirements.txt`.

After importing the `LLNL/dr_evt` repository into Read the Docs, configure:

- project name: `dr-evt`;
- configuration file: `.readthedocs.yaml`;
- default branch: `main`; and
- privacy level: public.

Read the Docs installs its GitHub webhook during import. A push to an active
branch or tag then triggers a documentation build.

## Builds and versions

Build status and logs are available from the
[project dashboard](https://readthedocs.org/projects/dr-evt/) and
[build history](https://readthedocs.org/projects/dr-evt/builds/).

Read the Docs uses `latest` for `main`. To publish a release, push its Git tag
and activate that version from the project's **Versions** page. The default
published version can also be selected there.

## Troubleshooting

For a failed hosted build, inspect its log first. Common causes are:

- a documentation dependency missing from `docs/requirements.txt`;
- invalid Sphinx configuration in `docs/conf.py`;
- a referenced file that was not committed; or
- a path or cross-reference that is valid locally but not from the source
  page's location.

Reproduce the failure with the local build described in
[Documentation Development](../README.md). Use a clean virtual environment
and the Python version selected by `.readthedocs.yaml` when the hosted and
local results differ.

Read the Docs operational details are covered in its
[official documentation](https://docs.readthedocs.io/).
