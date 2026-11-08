# Scheduling Validation

DR_EVT matched all 2,000 Lassen job starts and completions in two recorded comparisons:
FCFS without backfill using Fiction with Flux Core's `sched-simple`, and
periodic hybrid backfill using native Slurm with pooled license planning.
Hybrid backfill with Fluxion has similar resource-use curves but different
starts; a four-job reproducer isolates a node-binding cause.

These are observations for the recorded workloads and configurations. In the
Slurm experiment, the order of main scheduling and backfill was not enforced.
A later run exposed an order that DR_EVT does not reproduce, so the exact
match does not establish general scheduler equivalence.
See [Scheduling Loops](SCHEDULER_LOOPS.md) and
[Realism and Known Gaps](REALISM_AND_GAPS.md) for the supported policy sets.

## Workload and configuration

Both recorded-workload comparisons with Fiction use
[Lassen's job trace](https://github.com/LLNL/LAST) from
**2020-08-19 11:29:39 UTC** to **2020-08-21 04:06:09.138 UTC**, starting with
an empty cluster partition (`pbatch`) of **756 nodes**.
Each node has one core and jobs allocate whole nodes. Arrivals, time limits,
and actual durations come from the trace. These timestamps span the earliest
submission to the latest recorded completion; the simulated schedules can
finish at different times.

Both schedulers use equal priorities, FCFS order, no system latency or node
release delay, and an unlimited planning horizon. Fair share, usage accounting,
preemption, GPUs, and heterogeneous topology are excluded.
DR_EVT uses `--per_pass_accounting` to select the event-driven placement
implementation representing Flux's passes: immediate starts and reservations
count toward Q, and Q/R reset each pass. The resource-time reservation rule is
`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`.

Fiction fires arrivals and completions in simulation-time order. After each
event batch, it waits for scheduler decisions and resulting starts before
advancing time. Starts determine completion events at `start + actual duration`.
The native scheduler owns the waiting queue and resource allocations.
Read-only synchronization and resource-snapshot extensions support this handshake
without changing allocation, scheduling, or backfill rules.

## FCFS without backfill: identical sched-simple run

Both engines use FCFS without backfill, with Q=64. For `sched-simple`, Q bounds
outstanding allocation requests, rather than jobs examined per pass. It stops
at the first blocked queue head; jobs behind that head cannot start early.
It uses a simple node/core pool and assigns concrete resources at job start.
`sched-simple` has no backfill: its forecast estimates starts in a copied pool
without protecting future nodes in scheduling state.

All **2,000 start and completion times match at millisecond output precision**.
The resource-manager curves and waiting-time metrics are identical.
All **2,925 native resource snapshots** pass capacity and node-ownership checks.

| Metric | dr_evt | fiction with sched-simple |
|---|---:|---:|
| Mean waiting time | 11.4116 h | 11.4116 h |
| Makespan | 41.1710 h | 41.1710 h |
| Area under resource curve | 81,305,711.274 node-seconds | 81,305,711.274 node-seconds |
| Utilization | 72.5613% | 72.5613% |
| Process runtime | 0.0334 s | 145.849 s |

Process runtime measures executable launch through exit. Fiction includes Flux
startup, synchronization, resource snapshots, and postprocessing. These are
single validation-run timings, with instrumentation enabled.

:::{figure} ../_static/scheduler-validation/sched-simple/resource-curves.svg
:alt: Identical dr_evt and Fiction allocated-node curves for 2000 Lassen jobs under FCFS without backfill, with zero difference at every sampled simulation time.
:width: 100%

FCFS without backfill: identical resource use throughout the run.
:::

:::{figure} ../_static/scheduler-validation/sched-simple/comparison-metrics.svg
:alt: Identical waiting-time summaries and cumulative distributions, utilization, makespan, and resource area for the two FCFS runs; all 2000 waiting-time differences are zero.
:width: 100%

FCFS without backfill: identical waiting-time and utilization metrics.
:::

Thirty smaller comparisons also match at Q=1, Q=2, and Q=64. They cover blocked
heads, simultaneous events, early completions, and mixed queues.

## Hybrid backfill with Fluxion

The same 2,000 Lassen jobs use hybrid backfill **(R=32,Q=64)** and Fluxion's
**`lonodex` (low-node-ID-first)** resource matching. DR_EVT protects available
node counts over time; Fluxion reserves particular nodes.

Both runs use the same resource area, and all **2,920 native snapshots** pass
capacity and ownership checks. The area between curves is **2,947,190.112
node-seconds**, or **2.6612%** of the shared capacity-time window. The cause of
every trace difference has not been isolated.

| Metric | dr_evt | fiction with Fluxion |
|---|---:|---:|
| Mean waiting time | 10.1647 h | 10.5460 h |
| Makespan | 40.2895 h | 40.6922 h |
| Area under resource curve | 81,305,711.274 node-seconds | 81,305,711.274 node-seconds |
| Utilization | 74.1489% | 73.4151% |

Utilization is `100 × resource area / (nodes × makespan)` for each run.
The area between curves integrates the absolute allocated-node difference;
its percentage divides by `nodes × shared window duration`. Native ownership
checks separately rule out jobs claiming the same node.

:::{figure} ../_static/scheduler-validation/fluxion/resource-curves.svg
:alt: Full dr_evt and Fiction allocated-node curves for hybrid backfill with reservation depth 32 and queue depth 64, showing closely following resource use over simulation time.
:width: 100%

Hybrid backfill: similar resource use with differences in when nodes are occupied.
:::

:::{figure} ../_static/scheduler-validation/fluxion/comparison-metrics.svg
:alt: Waiting-time and utilization comparison for the hybrid runs, including fiction waiting time minus dr_evt waiting time; Fiction has a longer makespan and lower utilization despite equal resource areas.
:width: 100%

Hybrid backfill: aggregate metrics and the distribution of waiting-time differences.
:::

## Minimal reproducer for backfill start-time differences

The following reproducer has **four jobs on four nodes total**, one core per
node. Jobs use FCFS order, durations equal time limits, Q=2048, and no horizon
or simulated release delay. The queue cap exceeds the entire workload, so
per-pass resets do not explain this difference.

:::{figure} ../_static/scheduler-validation/four-job-backfill-divergence.png
:alt: Four-job example using low-node-ID-first matching. Fluxion reserves nodes 0, 1, and 2 for head job C at 80 seconds, so backfill job D waits until 80; a feasible dr_evt count-based schedule starts D at 70 while preserving C's start at 80.
:width: 100%

Specific-node reservations delay D by 10 seconds while preserving C's start.
:::

With `lonodex`, A occupies nodes 0,1 until 70; B occupies nodes 2,3 until 80.
C protects nodes 0,1,2 for 80–90. At 70 the free nodes 0,1 are both protected,
so Fluxion cannot start D there for its 50-second duration. D waits until node
3 becomes available at 80. DR_EVT starts D at 70 while preserving three-node
capacity for C at 80. The lower diagram shows one feasible physical assignment
for that count-based decision; DR_EVT itself does not emit node IDs.

Swapping A and B's equal-time submission order produces matching schedules.
Both low-ID-first and high-ID-first controls are verified under EASY, hybrid
R=32, and conservative backfill. Each matcher has three expected divergences,
three matching swaps, and twelve matching single-job deletion controls.
Four is the smallest verified workload from this reduction.

Matching controls start D at the releasing job's completion. Together, these
controls identify node-reservation placement as this reproducer's cause;
they do not explain every Lassen difference or all RPC-ordering effects.
See the [reproducer details](https://github.com/LLNL/dr_evt/blob/main/validation/flux/DIVERGENCE.md).

## Periodic backfill with native Slurm

This comparison uses native **Slurm 26.11.0-0rc2** from `external/slurm` and
DR_EVT's `FCFSPeriodicHybridCircularScheduler`, implemented in
`scheduler_fcfs_periodic_hybrid_circular.cpp`. It uses the same chronological
**2,000-job prefix** of [Lassen's job trace](https://github.com/LLNL/LAST),
starting with an empty `pbatch` partition capacity of **756 nodes**.
**1,731 recorded runtimes are rounded up to whole seconds in both inputs**;
arrivals and whole-minute time limits are unchanged.

### Why use bf_licenses?

Ordinary Slurm node backfill protects the FCFS head job's planned start through
two parts of its reservation:

- **Resource capacity:** enough resources must remain available throughout
  the job's planned execution interval.
- **Node binding:** the reservation identifies the specific compute nodes
  that supply this capacity. Later jobs must preserve their availability.

These are coupled parts of a node reservation: reserving those nodes protects
both their capacity and their identities. DR_EVT reserves capacity counts
without node binding. Our setup removes competition for specific nodes so we
can compare that count-based protection with Slurm.

Each trace job requests two separate resources from Slurm:

| Slurm resource | Role in this experiment |
|---|---|
| One private dummy node per job | Satisfies Slurm's compute-node requirement; no other job requests this node |
| Shared `capacity` licenses | Represent the 756-node pool; a 32-node trace job requests 32 licenses |

The licenses are interchangeable resource counts, not actual software use.
**Both the license-on and license-off runs use this same setup.** Reserving a
job's private dummy node cannot protect its share of the shared license pool.
Thus, private dummy nodes remove shared-node binding in both runs, while
`bf_licenses` controls reservation of shared capacity counts:

- **On:** reserve license counts for waiting jobs' planned execution intervals.
  Later jobs may start only if enough capacity remains for those reservations,
  matching DR_EVT's protection of future starts.
- **Off:** enforce current license availability without reserving future counts.
  Later jobs can consume capacity needed at an earlier job's planned start and
  delay it. This is an **experimental control**, not ordinary Slurm node
  backfill behavior, because our dummy nodes provide no shared-capacity protection.

The **license-on run is the intended comparison** with DR_EVT's protected
backfill and is closer to ordinary Slurm's protection of planned starts.
Slurm defaults to `bf_licenses` off, but ordinary node reservations already
protect compute capacity by reserving specific nodes. `bf_licenses`
adds future planning for a separate resource, normally software-license
counts. Our setup repurposes licenses as shared node capacity, so we need it
on to retain capacity protection after removing shared-node binding.

Ordinary Slurm's reservations of specific nodes can still cause the same
kind of divergence from DR_EVT's count-based reservations demonstrated by
the Fluxion four-job example. The exact differences depend on node selection
and workload. **The recorded license-on match demonstrates agreement for
that run with abstract capacity. It does not establish agreement for other
scheduler orderings or ordinary Slurm node placement.**

Slurm's native backfill and `select/cons_tres` remain in use;
`sched-simple`, used in a separate Flux control, also removes backfill.

### Running native Slurm with an adapter and virtual time

The [validation adapter](https://github.com/LLNL/dr_evt/tree/main/validation/slurm/licenses)
runs a native `slurmctld` and its allocation, selection, and backfill plugins.
It registers virtual nodes and sends native allocation, completion, and epilog
RPCs. Slurm owns the waiting queue, license allocations, and reservations;
there are no real compute-node daemons or running job processes.

The adapter replaces wall-clock reads with **virtual integer seconds**. It
advances to the next arrival, completion, or 30-second backfill tick, supplies
the event batch, and waits for stable native job states before advancing again.
A completion occurs at the observed start plus the recorded actual duration.
Host execution time does not advance simulation time, and the real monotonic
clock remains available for RPC handling. A small synchronization hook
serializes event batches with native backfill cycles; the allocation and
reservation algorithms are retained. The source hooks and binary hashes are
recorded with the results.

Both configurations use **R=32**, **Q=64**, a **72-hour planning horizon**,
and **one-second planning resolution**. Slurm's `sched_min_interval` and
`batch_sched_delay` are zero, and `defer` is omitted. DR_EVT uses
`--dispatch_interval 0 --backfill_interval 30` to select event-driven FCFS
dispatch with periodic backfill. Arrivals and completions request Slurm's main
dispatch; timer ticks run backfill. With `bf_licenses` on, main dispatch stops
when a job lacks enough free licenses. With it off, main dispatch considers
later jobs instead; DR_EVT's strict FCFS dispatch stops at the first job that
cannot fit. In the saved runs, the adapter gates backfill during event batches
but allows main dispatch between individual RPCs, whereas DR_EVT applies all
simultaneous events before dispatch.

Native state and license snapshots check capacity, private node ownership,
exact runtimes, and completed jobs. Independent audits also verify that both
schedulers' resource traces agree with their job records.

#### Event ordering and the optional dispatch gate

Main scheduling tries to start waiting jobs when jobs arrive or finish.
Backfill periodically looks for additional jobs that can run. When both are
ready at the same timestamp, DR_EVT runs main scheduling first. Slurm's two
schedulers have independent triggers and can run in either order. The order
can change which jobs start because backfill examines a limited queue prefix.

The adapter controls when jobs arrive and finish. Its optional gate prevents
scheduling while simultaneous events are being supplied. It then releases
both schedulers together, without choosing which runs first. **Event batching
is controlled; main-versus-backfill order is not.** Both the original exact
license-on match and the exact diagnostic license-off match are recorded
outcomes under this limitation. Two delayed starts in one gated license-on
run demonstrate an order DR_EVT does not reproduce; they do not bound the
possible divergence. Enforcing main scheduling before backfill would test
DR_EVT's specific order, rather than all native Slurm behavior.

**An arrival puts a job into the waiting queue; it does not start the job.**
In DR_EVT's preloaded trace, jobs are stored ahead of time and become eligible
at arrival. Known same-time arrivals are made eligible and all completions
and node releases at that time are processed before scheduling. In the
periodic-backfill model, FCFS dispatch precedes backfill selection. Streaming
callers can supply arrivals in a separate batch after advancing through
completions.

Native Slurm can schedule between separate arrival or completion messages.
Its main scheduler and backfill have independent triggers, so there is no
general guarantee that main dispatch runs first.

The adapter's opt-in `--batch-main-dispatch` mode adds Slurm's `defer` option
to queue arrivals without immediate allocation. It holds both main dispatch
and backfill while all same-time event RPCs are supplied, then releases the
gates. This matches DR_EVT's **batching of known events**, but **does not force
main dispatch to run before backfill**. It is a simulation timing control,
not a claim that real Slurm always batches events this way. Native placement
and reservation rules are retained. Eight native controls pass. The exact
license-on comparison below retains the original adapter without this gate
and with `defer` omitted. The [diagnostic subpage](SLURM_LICENSE_OFF_DIAGNOSTICS.md)
records the gated reruns and their scheduler-ordering control. The final
license-off run also waits for a completed main-dispatch acknowledgement
before advancing virtual time and preserves the subsecond clock on repeated
timestamps. The gated license-on control predates those two adapter corrections.

### Lassen results with bf_licenses on

DR_EVT was rerun against the saved, audited native schedule. All **2,000 jobs**
pass the DR_EVT checks, and **7,527 native snapshots** pass the Slurm audit.
Every **start and completion time matches exactly**, with **zero area between
the resource curves**.

| Metric | DR_EVT | Slurm, bf_licenses on |
|---|---:|---:|
| Mean waiting time | 10.3973 h | 10.3973 h |
| Makespan | 40.6236 h | 40.6236 h |
| Area under resource curve | 81,309,892 node-seconds | 81,309,892 node-seconds |
| Utilization | 73.5429% | 73.5429% |

:::{figure} ../_static/scheduler-validation/slurm/comparison-metrics.svg
:alt: Six-panel comparison with bf_licenses enabled, showing identical waiting-time distributions, utilization, makespan, resource area, and job start times.
:width: 100%

Native Slurm with license-aware backfill planning and DR_EVT's periodic hybrid model.
:::

[Six-panel PDF](../_static/scheduler-validation/slurm/comparison-metrics.pdf)
· [Resource-curve overview](../_static/scheduler-validation/slurm/comparison.pdf).

The [license-off diagnostic subpage](SLURM_LICENSE_OFF_DIAGNOSTICS.md)
groups the license-off Lassen control and both four-job scenarios with
protected DR_EVT, then presents the no-reservation DR_EVT comparison used to
test the remaining sources of divergence.

```{toctree}
:maxdepth: 1

SLURM_LICENSE_OFF_DIAGNOSTICS
```

### Sources of divergence and corrections

Two DR_EVT fixes made the original license-on comparison match all 2,000
jobs: keeping the next backfill tick after jobs start, and checking capacity
at the candidate's exact end time. They are described below. The two differences
in the newer gated license-on run come from a separate main-versus-backfill
ordering difference in the adapter experiment; these fixes remain in place.

**A backfill tick was skipped after jobs started.** Each periodic pass examines
a bounded queue prefix. Starting jobs removes them from that prefix and brings
later waiting jobs into view. DR_EVT could treat the queue as unchanged after
those starts and skip the next tick, so newly exposed jobs waited longer than
in Slurm. Starting a job now marks the scheduling state as changed, allowing
the next tick to examine the new prefix. In the recorded example, job 243
starts at **5,820 seconds** in both engines after this correction.
The [earlier plot](../_static/scheduler-validation/slurm/divergence/skipped-tick-before-fix/first-divergence.pdf)
shows the behavior before the fix.

**DR_EVT allowed a fit at an endpoint where Slurm rejected it.** Suppose a
candidate needs all available capacity until time 100, and another job has a
reservation starting at exactly 100. DR_EVT previously allowed the candidate
to end at 100 without checking the capacity change at that instant. Slurm
includes that change when checking the fit, so it may reject the candidate.
This difference affected both starting a job now and choosing a future
reservation time. DR_EVT now checks capacity at the requested end in both
searches. Actual job durations and capacity reservations are unchanged:
a reservation still releases its capacity at its end. The stricter fit check
is the [endpoint rule](SCHEDULER_LOOPS.md#shared-terms), rather than an added
execution or node-release delay.

An independent Python reference reproduced the entire pre-endpoint-fix
DR_EVT schedule. Changing only its endpoint rules gave these results against
the same audited native Slurm schedule:

| Endpoint checks | Differing starts | Area between resource curves |
|---|---:|---:|
| Previous rule | 1,266 | 1,204,904 node-seconds |
| Immediate admission corrected | 806 | 909,424 node-seconds |
| Admission and reservation placement corrected | 0 | 0 node-seconds |

The C++ implementation independently confirms the last row. The first admission
mismatch was job 605 at 24,570 seconds; after correcting admission alone, job 816
first differed at 39,870 seconds because of job 802's reservation placement.
A regression checks both endpoint searches.

:::{figure} ../_static/scheduler-validation/slurm/divergence/endpoint-rule-comparison.svg
:alt: Controlled endpoint experiment reduces differing starts from 1266 to 806 when admission is corrected, then to zero when future reservation placement is also corrected.
:width: 100%

Both endpoint searches must agree with Slurm to reproduce this workload.
:::

[Controlled experiment PDF](../_static/scheduler-validation/slurm/divergence/endpoint-rule-comparison.pdf)
· [Exact experiment data](../_static/scheduler-validation/slurm/divergence/endpoint-comparison.json)
· [Historical job-605 diagnostic](../_static/scheduler-validation/slurm/divergence/first-divergence.pdf).

The corrections apply to event-driven dispatch with separate periodic
hybrid/conservative backfill. Other DR_EVT endpoint rules are unchanged.
Node release delay is zero in both engines throughout these experiments.

### License-enabled reference and corrected DR_EVT performance

The native reference is an uninterrupted 2,000-job run with `bf_licenses` on,
`info` logging, and no restarts or configuration changes. Its controller/adapter
phase took **716.8365 seconds**, including RPC, observation, and synchronization.
The complete validation process, including the earlier DR_EVT comparison and
plots, took **728.6231 seconds**. This audited reference is retained for the exact protected-model comparison.
The diagnostic subpage reports a fresh gated native run separately.

The current DR_EVT was timed in **five new uninterrupted local macOS runs** of
the same workload. All five produced byte-identical schedules and resource
traces, matching native Slurm.

| Corrected DR_EVT process timing | Wall time |
|---|---:|
| Median | 0.080178 s |
| Minimum | 0.078113 s |
| Maximum | 0.085684 s |

The timed interval includes process launch, trace input, simulation,
schedule/resource output and exit. Python output parsing, audits and plotting
are excluded. Filesystem caches were warm from the preceding comparison run.
The saved native Slurm timing used the Linux validation container, so these
measurements have different execution environments and timing scopes.

[DR_EVT performance record](../_static/scheduler-validation/slurm/performance.json)
· [DR_EVT timing script](../_static/scheduler-validation/slurm/measure_dr_evt.py)
· [Native performance record](../_static/scheduler-validation/slurm/native-performance.json).

## Evidence and reproduction

The figures and tables report DR_EVT validation experiments. Scheduler names
identify the tested software; they do not imply upstream endorsement of the
results. Source attribution and upstream license links are listed in
[Scheduling Loops](SCHEDULER_LOOPS.md#sources).

The [Fiction container harness](https://github.com/LLNL/dr_evt/blob/main/validation/flux/README.md)
contains commands for both native schedulers, the recorded trace, resource
audits, and the minimal reproducer. Raw Lassen source logs
and the separate reservation-reset investigation are not part of these results.

Published figure folders contain the standalone plotting script, saved inputs,
exact plotting numbers, summary CSVs, and resource-manager curve CSVs:

- sched-simple: [plotting script](../_static/scheduler-validation/sched-simple/plot_comparison.py),
  [inputs](../_static/scheduler-validation/sched-simple/plot-input.json),
  [numbers](../_static/scheduler-validation/sched-simple/plot-data.json),
  [summary](../_static/scheduler-validation/sched-simple/plot-summary.csv),
  [provenance](../_static/scheduler-validation/sched-simple/provenance.json).
- Fluxion: [plotting script](../_static/scheduler-validation/fluxion/plot_comparison.py),
  [inputs](../_static/scheduler-validation/fluxion/plot-input.json),
  [numbers](../_static/scheduler-validation/fluxion/plot-data.json),
  [summary](../_static/scheduler-validation/fluxion/plot-summary.csv),
  [provenance](../_static/scheduler-validation/fluxion/provenance.json).
- Slurm, bf_licenses on: [plotting script](../_static/scheduler-validation/slurm/plot.py),
  [inputs](../_static/scheduler-validation/slurm/plot-input.json),
  [numbers](../_static/scheduler-validation/slurm/plot-data.json),
  [provenance](../_static/scheduler-validation/slurm/provenance.json).
- Slurm, bf_licenses off: [plotting script](../_static/scheduler-validation/slurm/bf-licenses-off/plot.py),
  [inputs](../_static/scheduler-validation/slurm/bf-licenses-off/plot-input.json),
  [numbers](../_static/scheduler-validation/slurm/bf-licenses-off/plot-data.json),
  [provenance](../_static/scheduler-validation/slurm/bf-licenses-off/provenance.json).
- Slurm controls and divergence: [four-job plotting script](../_static/scheduler-validation/slurm/controls/plot_timeline.py),
  [original four-job data](../_static/scheduler-validation/slurm/controls/four-job-node-assignments-data.json),
  [reservation-protection data](../_static/scheduler-validation/slurm/controls/reservation-protection-node-assignments-data.json),
  [endpoint experiment plotting script](../_static/scheduler-validation/slurm/divergence/plot_endpoint_comparison.py).

With NumPy and Matplotlib installed, download a complete figure folder and run:

```sh
python3 plot_comparison.py --plot-input plot-input.json
```

For either Slurm Lassen figure folder, run `python3 plot.py plot-input.json`.
For the control folder, run `python3 plot_timeline.py . --case pooled` and
`python3 plot_timeline.py . --case protection`. For the divergence folder,
run `python3 plot_endpoint_comparison.py endpoint-comparison.json`.

The Fiction comparisons use DR_EVT revision `57c3267`, Flux Core `d0309fcf0`,
Fluxion `9eba598b`, and Fiction `346cbd65`. Machine-readable evidence is in
[validation/flux/evidence](https://github.com/LLNL/dr_evt/tree/main/validation/flux/evidence).
The DR_EVT simulator snapshot used for these experiments is recorded by its
[source hashes](../_static/scheduler-validation/slurm/dr_evt-sources.json)
and [source patch](../_static/scheduler-validation/slurm/source.diff.gz).
The original native reference retains its own
[provenance](../_static/scheduler-validation/slurm/provenance.json).
The saved native Slurm reference uses Slurm `17abb848e`
(26.11.0-0rc2). The [Slurm harness](https://github.com/LLNL/dr_evt/blob/main/validation/slurm/README.md)
records source revisions, clock and synchronization hooks, workload conversion,
audits, and both license-planning settings. Experiment results and plotting
artifacts are stored first under `validation/slurm/`; published figures are
copied from those audited results.
