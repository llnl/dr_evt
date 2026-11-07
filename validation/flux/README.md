# Fluxion validation with Flux Fiction

This harness builds Flux Core, Fluxion, Flux Fiction, and DR_EVT in a Linux
container. It verifies the Fiction backend and time-step synchronization with
hand-computed smoke cases. Paired DR_EVT comparisons are a separate step.

## Run

From the repository root:

```sh
bash validation/flux/run.sh
```

Sources default to `external/{flux-core,flux-sched,flux-fiction}`;
`FLUX_SOURCES` selects another checkout directory. The checkouts and DR_EVT
repository are mounted read-only. Only committed sources are exported into a
reusable Docker volume, and local checkout edits are excluded.

`FLUX_VALIDATION_IMAGE` selects the image name, `FLUX_VALIDATION_BUILD_VOLUME`
selects the build volume, and `BUILD_JOBS` controls Flux build parallelism.
Use a fresh build volume after changing external revisions or the local
synchronization overlay. Results default to `validation/flux/results/`;
`FLUX_VALIDATION_RESULTS` selects another directory. Generated results are ignored.

`prepare.py` records external revisions and overlay hashes in `sources.json`
and saves the applied patch as `fluxion-quiescence.patch`. `build_dr_evt.py`
exports the current DR_EVT commit, builds a Release executable with the default
resource-time reservation rule, and records its revision, configure flags, and
binary hash in `dr_evt-sources.json`. Each Fiction run retains its input,
configuration, log, and job transitions in a unique output directory.

## Checks

The default runner performs:

- Native synchronization checks for idle queues, allocations, pending jobs,
  resource releases, cancellation, and safe backfill.
- Fiction smoke cases with hand-computed start and finish times: one job, and
  a blocked four-node head with a safely backfilled one-node job.
- A build of the committed DR_EVT simulator for subsequent comparisons.

The synchronization checks use one broker with four cores. Fiction smoke
cases use **four nodes total, one core per node**, whole-node allocation,
equal priorities, known time limits, and no simulated system latency.
Reports are `quiescence.json` and `fiction/report.json`; the complete build
log is `build-and-test.log`. Agreement is established only for the exercised
cases, not general scheduler equivalence.

## Time-step synchronization

Fiction owns the time-ordered arrival/completion event queue; Fluxion owns the
waiting-job queue. Fiction submits a job's resource requirements and time limit.
After receiving its start notification, Fiction schedules completion at
`start + actual duration`. Actual duration stays in Fiction.

After firing the events at one timestamp, Fiction asks its jobtap plugin to
confirm those events and their resulting starts. Jobtap sends an empty
`sched.quiescent` request to the local handler. The handler replies only when
Fluxion has no active or requested scheduling pass, outstanding allocation
responses and changed annotations have been sent, and allocation-request counts
agree with the job manager. Its reply contains `status` and `alloc_current`,
the number of allocated jobs. Jobtap then waits for Fiction's start
acknowledgments before echoing the original step request. Fiction can then
advance to the next event timestamp; waiting jobs may remain queued.

The handler is applied only to the isolated Fluxion export and observes state
without changing placement or scheduling policy. The tested setup has one
unrestricted queue with unheld jobs, and backfill examination caps cover its
waiting queues. Finite-depth restart behavior is outside these checks.
Concurrent probes and error handling are supported. Real-time timeouts stop
incomplete runs; they are not the synchronization condition.

Optional profiling dependencies are omitted from the image; they do not affect
scheduling.
