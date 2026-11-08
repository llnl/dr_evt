# Current native Slurm license-planning validation

The license-on native Slurm run was completed with the repeated-timestamp clock
fix and completed-main-dispatch acknowledgements. All 2,000 starts and completions
match protected DR_EVT, with zero area between resource curves. All 7,527 native
frames pass the independent audit. Native controller/adapter elapsed time is
697.835665 seconds, including startup, RPCs, observations, acknowledgements and
snapshots; builds, DR_EVT, audits and plotting are excluded.

License-off reuses the completed acknowledged native record from
`../gated-lassen-20261108/off/`. Both diagnostic and protected DR_EVT references
were rerun using the same current simulator binary as license-on. Diagnostic
DR_EVT matches all 2,000 starts/completions and resource curves; protected DR_EVT
differs in 1,943 starts. The 6,779 native frames pass the independent audit.
Native elapsed time remains the original 739.287749 seconds; this archive does
not claim a second native license-off run.

Native Slurm, backfill plugin, clock library and observation client binary hashes
are identical across these on/off comparisons. The current native source version
is Slurm 26.11.0-0rc2 at `17abb848e20054009d136a2c504a23e5462f26c8`.
The simulator snapshot is committed DR_EVT revision `d545578`; its exact build
flags, source hashes and binary hash are saved per comparison. Subsequent
clang-format and Doxygen changes reproduce identical adapter binaries, and the
formatted DR_EVT reproduces both recorded schedules. Fresh local timings record
that formatted source snapshot and its patch separately under `local-performance/`.

## Interpretation

Each job owns a private dummy compute node and requests its original node demand
as shared license counts. This removes competition for specific nodes in both
runs. `bf_licenses=on` protects future shared capacity; off is a diagnostic
control with only current-capacity limits. The option alone does not remove node
binding. Neither experiment represents ordinary Slurm placement on shared nodes.

The adapter holds both schedulers while supplying simultaneous events, then
releases both together. It does not enforce main scheduling before backfill as
DR_EVT does. These exact matches are observations for recorded runs, not proof
of agreement for every native order or a bound on possible divergence. The
older two-delay gated-on result and startup replay remain historical diagnoses
under `../gated-lassen-20261108/`.

## Contents and reproduction

`on/` and `off/` hold reports, audits, input/output CSVs, native configurations,
clock/gating overlays, captured scripts, source/binary hashes, compressed logs,
lossless native snapshot deltas and six-panel PDF/SVG/PNG plots. `off/` also saves
its original simulator metadata and the current refreshed references.
`protected-off-comparison/` compares the current protected reference against the
same native off run and has its own independent audit and figures.

`native-run-command.json` records the on-run invocation. For source downloads,
container construction, volume selection, resource/timing assumptions and the
complete harness command, see [the adapter README](../../README.md).
`ADAPTER_README.md` captures those considerations with this archive.
Regenerate a figure with `python3 plot.py plot-input.json` from its folder;
Matplotlib and NumPy are required. Repeat an audit with
`python3 scripts/audit_event.py .` from an on/off folder.

Five uninterrupted macOS timings for each formatted DR_EVT model are in
`local-performance/`. Every timed schedule and resource trace is byte-identical
to its preceding local comparison and agrees with the recorded native reference
for its policy. Native/Linux and DR_EVT/macOS timings have different execution
environments and scopes; no speedup claim is made.

CSV newlines and SVG trailing whitespace are normalized. Snapshot deltas preserve
every native frame; their original raw-stream hashes are saved beside them.
`archive-sha256.json` identifies every archived artifact.
