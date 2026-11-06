# Validating DR_EVT against Fluxion with Flux Fiction

The objective is to compare DR_EVT's existing event-driven FCFS and backfill
behavior with actual Fluxion scheduling decisions on the same small workloads.
Flux Fiction emulates execution and time advancement while using the real Flux
job manager and Fluxion scheduling modules. This is a validation plan; no paired
Flux Fiction/DR_EVT run has been completed yet.

## Reference stack and prerequisites

Pin the source revisions, build options, scheduler configuration, and emulator
configuration alongside every result. The inspected checkouts are:

- flux-core: `d0309fcf0e672d90e65b0797168e56a430b9fb57`
- flux-sched: `9eba598b4e0940d597695e65f20f5357fc513427`
- flux-fiction: `346cbd65bf7da3798e2a351b44e0f57a5b20da96`

Flux Fiction requires `sched.quiescent`, returning `status = 0` after scheduler
work settles and `alloc_current` for the current allocations. The inspected
flux-sched checkout contains no implementation of this RPC. Before running,
locate a compatible Fluxion revision or add and independently verify a
synchronization extension. Record any extension as part of the reference build;
do not change placement or reservation decisions to make the comparison pass.

Use the Linux container development setup supplied by Flux Fiction to build
flux-core, flux-sched, the native jobtap plugin, and the faketime dependency.
First run its smoke workload and prove that scheduler startup, submissions,
allocation/start acknowledgements, and completions settle before time advances.
Use the Flux backend, not Flux Fiction's mock backend.

## Common model

Use a cluster containing four identical nodes with one schedulable core per
node, no GPUs or storage resources, and exclusive whole-node jobs. Use a single
queue, equal priorities, and FCFS order. Do not enable fair share, accounting
policies, preemption, or cleanup delays. Fix ordering for simultaneous
submissions and verify that both schedulers receive that order.

Generate both input formats from one canonical workload containing stable job
IDs, submission offsets, node counts, requested time limits, and actual runtimes.
Flux Fiction accepts trace columns `JobID,NNodes,NCPUS,Timelimit,Submit,Elapsed`.
Use integer seconds initially, a fixed epoch, and exact planning resolution.
Requested limits drive reservations; actual elapsed times drive completions.

Start with an unlimited planning horizon. A finite DR_EVT horizon must not be
presented as a Fluxion equivalent until a corresponding reference control has
been established. Similarly, distinguish Flux job-manager outstanding allocation
request depth from Fluxion's policy queue depth.

Use faketime and compare logical simulation timestamps, not process wall time.
Audit `account_system_latency`, job-start batching, and
`quiescent_accumulation_window` against the pinned emulator implementation:
option names alone do not establish their timing effects. Start with a zero
accumulation window. Check how the scheduler observes the clock, including
reservation estimates, before requiring exact timestamp agreement. Realtime is
virtualized while monotonic timers remain real in the documented setup.

## Validation sequence

1. **FCFS baseline.** Run one job, sequential jobs, simultaneous arrivals, a
   blocked head, and a completion coinciding with an arrival. Verify queue
   order, starts, completions, and free-node accounting.
2. **Backfill baseline.** Use an occupied cluster, a waiting head, and a smaller
   lower-priority job. Test both a job that can backfill while preserving the
   head's reservation and one that must wait. Begin with hybrid reservation
   depth 1, then compare EASY separately.
3. **Queue depth.** Vary the Fluxion policy queue depth and DR_EVT
   `--backfill_queue_depth` across 1, 2, 3, and unlimited. Include a runnable
   candidate just outside the examined prefix. Check examinations per pass,
   repeated passes at the same logical timestamp, and whether starts expose
   further candidates. Differences are findings, not reasons to tune inputs
   until they disappear.
4. **Reservation depth.** Compare hybrid depths 1 and 2 and conservative
   backfill. Use a long low-priority job whose start is permitted with one
   protected reservation and prevented with two. Verify that conservative
   protection is bounded by the jobs actually examined. Confirm policy-specific
   handling of `reservation-depth` in the pinned Fluxion revision.
5. **Requested versus actual runtime.** Repeat with early completion. Separately probe overruns,
   recognizing that DR_EVT currently caps actual runtime at its time limit. Verify replanning and distinguish requested reservation estimates
   from actual release times.
6. **Repeatability and broader traces.** Repeat every small case, then generate
   seeded small workloads. Add complexity only after the simpler cases agree
   or have an explained, reproducible difference.

The existing four-node examples in [Scheduling Loops](SCHEDULER_LOOPS.md) are
useful backfill probes: a running job occupies three nodes until 120; queued
jobs request (2 nodes, 20 seconds), (4 nodes, 100 seconds), and (1 node,
200 seconds). Stage the running job before admitting the pending batch. Vary
queue depth and reservation depth without changing submission order.

## Outputs and acceptance

Retain the canonical workload, both generated inputs, full configurations,
source revisions, run logs, and normalized outputs. Match jobs by stable trace
ID rather than Flux-assigned IDs. Compare:

- Started, pending, and completed job sets at each settled event time.
- Per-job logical start and completion times and queue order.
- Settled free/allocated node counts and resource-capacity invariants.
- Reservation estimates and which lower-priority starts they protect, when the
  reference exposes enough state to establish these.

Ignore equivalent node identities on the homogeneous graph. Do not infer
reservation correctness solely from equal final makespan. If timestamp
normalization is needed, document its units and reason before comparison; never
choose a tolerance merely to conceal a differing placement decision.

Each case passes when its expected decisions and capacity invariants match in
repeated runs. For a mismatch, retain the smallest reproducer and classify the
cause: input/configuration mismatch, emulator synchronization, scheduling
semantics, or unresolved. Keep a result table with configuration, expected
behavior, observed behavior, and evidence paths.

This validates the tested Fluxion behavior and resource model. Flux Fiction
alone does not validate Slurm, LSF, Spur, or DR_EVT's unified periodic dispatch.
Test unified ticks against hand-computed schedules and an independent loop
reference; production cadence claims need evidence from the corresponding
scheduler.

## Source entry points

- Flux Fiction [integration contract](../../flux-fiction/README.md),
  [event engine](../../flux-fiction/src/flux_fiction/_core/engine.py), and
  [jobtap synchronization](../../flux-fiction/src/jobtap/emu-jobtap.c).
- Fluxion [qmanager policy tests](../../flux-sched/t/t1004-qmanager-optimize.t)
  and [reservation matcher tests](../../flux-sched/t/t3005-resource-rsv.t).
- Fluxion [resource-query utility](../../flux-sched/resource/utilities/README.md)
  can separately validate graph matching and reservation feasibility; it does
  not validate the complete qmanager scheduling loop.

These source links require the optional sibling checkouts. Keep cloned source
and third-party PDFs outside the DR_EVT change. Behavioral descriptions and the
comparison harness should be original; do not copy scheduler implementation
code into DR_EVT.
