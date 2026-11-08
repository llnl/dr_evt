# Gated native Slurm validation and no-reservation DR_EVT reference

The acknowledged license-off run matches diagnostic DR_EVT for every start
and completion of 2,000 Lassen jobs, with zero area between resource curves.
Its protected DR_EVT control differs in 1,943 starts. All 6,779 native frames
pass the runtime/capacity audit, dispatch acknowledgements, and the check that
no currently fitting eligible job remains waiting.

`on/` is the earlier gated license-on ordering control: two 30-second start
delays, fully reconstructed by charging head job 469 one backfill slot at
23,460 seconds. `ordering-diagnosis/` stores that controlled reference.
`missed-startup-dispatch/` preserves the first gated off attempt (972 differing
starts); `startup-diagnosis/` reproduces it by delaying only initial jobs 1/2
from zero to one second. `off/` uses the corrected clock and dispatch counter.
Native Slurm controller/backfill binaries are identical across on/off controls;
the off adapter's clock library and observation client differ, as recorded.
Both native comparisons use the same simulator binary and input jobs.

`original-license-on-comparison/` refreshes the original exact, ungated
license-on figures. `original-protected-comparison/` retains the original
ungated off comparison against protected DR_EVT. `controls/` contains eight
native controls, their audits and four-job assignment figures.

Snapshots use lossless job-record deltas (`job-deltas-v1`); audit_event.py
reconstructs complete native frames. The original uncompressed stream hashes
are saved beside them. Every compacted archive was independently re-audited.
`archive-sha256.json` covers normalized CSV/SVG files and all saved artifacts.
Raw streams remain under ignored validation/slurm/results/licenses/ folders.

`local-performance/` stores five uninterrupted macOS timings per DR_EVT model,
taken after native validation and tests exited. Medians are 0.080178 seconds
(protected) and 0.057316 seconds (diagnostic); outputs match their references.
The native controller/adapter timings are in native-performance.json and
include RPC, registration, observation and synchronization, not just scheduling.

Regenerate figures from saved inputs with plot.py, plot_timeline.py, and
plot_gated_diagnosis.py. The two diagnostic folders contain self-contained
replay scripts and data. Source hashes, the exact simulator snapshot patch,
native hooks, commands and adapter scripts record the versions actually run.

Exact source patches are gzip-compressed to preserve unified-diff context
whitespace. Decompress `source.diff.gz` before applying it to the recorded base.

## Scope of the observed matches

The adapter batches simultaneous events, then releases main scheduling and
backfill together. It does not enforce which runs first. DR_EVT runs main
scheduling first and does not reproduce every order Slurm can use. The exact
license-on and diagnostic license-off matches are observations for recorded
runs, not evidence of equivalence across native scheduling orders. The two
delayed starts in the gated license-on run do not bound possible divergence.
Enforcing main scheduling before backfill would test that specific ordering.
The gated license-on run predates the final adapter clock and acknowledgement
corrections and has not been rerun with them.
