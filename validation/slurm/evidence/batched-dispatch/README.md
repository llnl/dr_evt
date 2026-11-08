# Slurm adapter batching controls

All eight native controls and their independent audits pass with
`--batch-main-dispatch`. This includes simultaneous releases with license
planning enabled and disabled. Both releases become available before
scheduling, so the earlier two-license job starts before later one-license
jobs. This bundle does not include a new full Lassen run.

Each case preserves its workload, native and DR_EVT schedules, resource
curves, audit, configuration, binary hashes, compressed controller log,
and compressed native snapshots. `controls.json` combines the reports.
The four-job PDF/SVG/PNG plots and their data are retained at this directory's
root. Regenerate them with `plot_timeline.py` and `--case pooled` or
`--case protection`.

`scripts/` contains the captured experiment scripts shared by the cases;
case-specific script versions, if present, override the shared copy. The
original `scripts_sha256` entries in each provenance file identify those
versions. `event-overlay.json` and `dispatch-overlay.json` preserve the
native source hooks. Runtime clocks, binaries, and daemon state are omitted.
`archive-sha256.json` records the bundled file hashes.
CSV line endings and SVG trailing whitespace are normalized for version
control. Runtime output logs are compressed with their original bytes retained.

Audit a case from the repository root:

```sh
python3 validation/slurm/licenses/audit_event.py \
  validation/slurm/evidence/batched-dispatch/simultaneous-releases-off
```

The gates synchronize supplied same-time event batches. They retain native
placement and reservation rules and do not require main dispatch to run
before backfill after release. They are a validation timing control, not a
universal ordering guarantee for production Slurm.
