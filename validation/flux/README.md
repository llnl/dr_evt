# Fluxion validation with Flux Fiction

This harness builds Flux Core, Fluxion, and Flux Fiction in Linux containers.
It adds a local `sched.quiescent` handler to an isolated Fluxion source export.
The mounted checkouts are read-only; DR_EVT's scheduler is unchanged.

From the repository root:

```sh
bash validation/flux/run.sh
```

The default source location is `external/{flux-core,flux-sched,flux-fiction}`.
Override it with `FLUX_SOURCES=/path/to/checkouts`. Results and build logs go
to `validation/flux/results/`; `FLUX_VALIDATION_RESULTS` overrides that path.
The reusable Docker build volume is `dr-evt-flux-validation-build`; override
it with `FLUX_VALIDATION_BUILD_VOLUME` to start a fresh build after a source or
extension change. Only committed external sources are exported, and their
revisions and extension hashes are recorded.

## Synchronization extension

Flux Fiction waits for `sched.quiescent` before advancing its logical clock.
The handler waits until Fluxion has no active matching loop, requested pass,
unsent allocation response, or dirty annotation. It also queries
`job-manager.alloc-query` until the job manager has sent its queued batch and
its outstanding allocation count matches Fluxion's waiting-job count.
Reservations remain waiting jobs; current allocations count running jobs.
Flux Fiction separately waits for the corresponding start acknowledgements.

The extension observes state without invoking extra scheduling passes or
changing placement, queue depth, reservation depth, or backfill admission.
It supports concurrent probes and returns errors on query failure or unload.
Use it with a single unrestricted queue and unheld jobs: held requests left
in the job manager prevent a successful drain. The test runner has a real-time
timeout to detect stalls; waiting a fixed duration is not the readiness rule.

The native tests exercise the actual RPC on a four-core graph with FCFS,
EASY, hybrid, and conservative policies. They cover concurrent idle probes,
initial allocation, blocked batches, cancellation, and EASY reservation/backfill.
FCFS uses queue depth one to check repeated release-triggered passes;
the ordinary backfill cases use depth 100.

The cloned Fluxion revision continually retries a truncated reservation-only
backfill pass: reservations mark the queue as scheduled, which requests another
pass when the depth is exhausted with candidates remaining. An explicit
depth-one EASY case verifies that the handler stays pending during these
retries and resumes when cancellation allows the scheduler to settle. This
limitation is retained rather than changing the reference scheduling policy.

Two Flux Fiction smoke cases use its real Flux backend, a four-node graph,
and libfaketime to check hand-computed job starts, completions, and allocated
node counts. They establish the runnable reference and synchronization
prerequisites.

On November 6, 2026, all 24 native synchronization cases and both Fiction smoke
cases passed in Ubuntu 24.04 ARM64 Docker with GCC 13. The tested build volume
is `dr-evt-flux-validation-build-v3`; set `FLUX_VALIDATION_BUILD_VOLUME` to that
value to reuse it locally. Results include `sources.json`, the applied
`fluxion-quiescence.patch`, `quiescence.json`, and Fiction inputs, logs, and
transition tables under `results/fiction/`.

Optional profiling dependencies are omitted from the image. They do not
affect scheduling and the upstream DFTracer package failed to compile on
the initial ARM64 Docker setup.
