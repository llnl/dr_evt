# License-off diagnostic evidence

This bundle reconstructs the saved 2,000-job license-off experiment. It does
not contain a new native Lassen run. The serial-event replay matches every
native start and completion; the other variants isolate future capacity
reservation, main dispatch, and event batching. See `FINDINGS.md` for scope.

`diagnosis.json` records comparisons, input hashes, and native log excerpts.
`plot-data.json` and the four schedule CSVs contain the replay output.
`native-off/` preserves the original native report, audit, configuration,
provenance, and compressed controller log. `corrected-report.json` contains
the endpoint-corrected DR_EVT comparison, and `reference.py` is the saved
inclusive-search reference. Original provenance paths are retained.

Reconstruct in a fresh output directory, from the repository root:

```sh
python3 validation/slurm/licenses/diagnose_license_off.py \
  validation/slurm/evidence/license-off-diagnosis/corrected-report.json \
  /tmp/slurm-license-off-replay \
  --reference validation/slurm/evidence/license-off-diagnosis/reference.py \
  --native-run validation/slurm/evidence/license-off-diagnosis/native-off
```

Regenerate the PDF, SVG, and PNG with matplotlib:

```sh
python3 validation/slurm/licenses/plot_license_off_diagnosis.py \
  validation/slurm/evidence/license-off-diagnosis
```

Copied scripts make this snapshot reproducible independently of later
changes to the repository scripts. `archive-sha256.json` records file hashes.
Generated CSV line endings and SVG trailing whitespace are normalized for
version control; values and plotted schedules are unchanged.
