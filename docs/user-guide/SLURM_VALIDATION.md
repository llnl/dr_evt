# Slurm Simulation Validation

The recorded license-on run matches all 2,000 job starts and completions.
The adapter leaves main-versus-backfill order uncontrolled; another run
exposed a difference caused by that order.

## Workload and configuration

This comparison uses native **Slurm 26.11.0-0rc2** from `external/slurm` and
DR_EVT's [`FCFSPeriodicHybridCircularScheduler`][cpp-fcfsperiodichybridcircularscheduler], implemented in
`scheduler_fcfs_periodic_hybrid_circular.cpp`. It uses the
[common 2,000-job Lassen workload](SCHEDULER_VALIDATION.md#lassen-workload).
**1,731 recorded runtimes are rounded up to whole seconds in both inputs**;
arrivals and whole-minute time limits are unchanged.

### Why use bf_licenses?

Slurm normally reserves specific compute nodes to protect a waiting job's
planned start. DR_EVT reserves interchangeable capacity counts. This experiment
suppresses the effects of resource binding by removing competition for node
identities while retaining future capacity protection:

| Slurm resource | Role in this experiment |
|---|---|
| One private dummy node per job | Satisfies Slurm's compute-node requirement; no other job requests it |
| Shared `capacity` licenses | Represent the 756-node pool; a 32-node trace job requests 32 licenses |

The licenses represent capacity counts, not software use. Both license-on and
license-off runs use this setup. Reserving a private node cannot protect shared
license capacity, so `bf_licenses` determines that protection:

- **On:** reserve license counts throughout waiting jobs' planned intervals.
  This is the intended comparison with DR_EVT's protected backfill.
- **Off:** check current license availability without protecting future counts.
  Later jobs can delay an earlier job's planned start. This is a diagnostic
  control; ordinary Slurm node backfill still protects capacity by reserving nodes.

Slurm defaults to `bf_licenses` off. We turn it on to protect shared capacity
after removing node competition. Native backfill and `select/cons_tres` remain
in use.

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
`batch_sched_delay` are zero. The current adapter enables `defer` to queue
arrivals until their simultaneous event batch is complete. DR_EVT uses
`--dispatch_interval 0 --backfill_interval 30` to select event-driven FCFS
dispatch with periodic backfill. Arrivals and completions request Slurm's main
dispatch; timer ticks run backfill. With `bf_licenses` on, main dispatch stops
when a job lacks enough free licenses. With it off, main dispatch considers
later jobs instead; DR_EVT's strict FCFS dispatch stops at the first job that
cannot fit. The dispatch gate below batches simultaneous events.

Native state and license snapshots check capacity, private node ownership,
exact runtimes, and completed jobs. Independent audits also verify that both
schedulers' resource traces agree with their job records.

#### Event ordering and the optional dispatch gate

Main scheduling tries to start waiting jobs after arrivals and completions.
Backfill periodically looks for additional jobs that can run. DR_EVT makes
all known same-time arrivals eligible, processes completions and releases,
then runs main scheduling before backfill. Slurm's schedulers have independent
triggers and can run in either order. That order can change starts because
backfill examines a limited queue prefix.

The adapter's `--batch-main-dispatch` mode enables Slurm's `defer` option and
holds main scheduling and backfill while supplying all same-time event RPCs.
It then releases both gates together. **It controls event batching, but does
not force main scheduling to run first.** This is a simulation timing control;
native placement and reservation rules are retained.

Both current published comparisons use this gate and the corrected clock.
Eight native controls pass. The adapter waits for a completed main-dispatch
acknowledgement before advancing virtual time. When a timestamp repeats, it
preserves the subsecond counter so Slurm does not see time move backward.

The current license-on run matches DR_EVT exactly. An earlier gated run had
two delayed starts caused by a scheduler order DR_EVT does not reproduce.
Those two differences do not bound divergence in other runs. The
[diagnostic subpage](SLURM_LICENSE_OFF_DIAGNOSTICS.md#gated-license-on-control-main-dispatch-versus-backfill-order)
records that run. Enforcing main scheduling before backfill would test DR_EVT's
specific order rather than all native Slurm behavior.

## Results

### Lassen results with bf_licenses on

Native Slurm was rerun with the current adapter, including the clock fix and
completed-dispatch acknowledgements, and compared with the current DR_EVT
build. All **2,000 jobs** pass the DR_EVT checks, and **7,527 native snapshots**
pass the Slurm audit.
Every **start and completion time matches exactly**, with **zero area between
the resource curves**.

| Metric | DR_EVT | Slurm |
|---|---:|---:|
| Mean waiting time | 10.3973 h | 10.3973 h |
| Makespan | 40.6236 h | 40.6236 h |
| Area under resource curve | 81,309,892 node-seconds | 81,309,892 node-seconds |
| Utilization | 73.5429% | 73.5429% |
| Process runtime | 0.036436 s (median of 5 runs) | 697.8357 s |

DR_EVT timing covers process launch, input, simulation, output, and exit in
the common Linux Docker environment on an Apple M3 Max. Slurm's single recorded
controller/adapter timing also includes startup, RPCs, observation, and
synchronization in its Linux Docker container on the same host. Both exclude
builds, audits, and plotting. [Measurement details](#execution-time) are below.

:::{figure} ../_static/scheduler-validation/slurm/comparison-metrics.svg
:alt: Six-panel comparison with bf_licenses enabled, showing identical waiting-time distributions, utilization, makespan, resource area, and job start times.
:width: 100%

Native Slurm with license-aware backfill planning and DR_EVT's periodic hybrid model.
:::

[Six-panel PDF](../_static/scheduler-validation/slurm/comparison-metrics.pdf)
· [Resource-curve overview](../_static/scheduler-validation/slurm/comparison.pdf).

The [license-off diagnostics](SLURM_LICENSE_OFF_DIAGNOSTICS.md) compare protected
and no-reservation DR_EVT policies using Lassen and four-job controls.

### Execution time

The current native reference is an uninterrupted 2,000-job rerun with
`bf_licenses` on, the corrected adapter clock, dispatch acknowledgements,
`info` logging, and no restarts or configuration changes. Its controller/adapter
phase took **697.8357 seconds**, including startup, RPCs, observation and
synchronization; DR_EVT execution, audits, plotting and the build are excluded.
The license-off native reference took **739.2877 seconds** and uses identical
Slurm and adapter binaries. Both DR_EVT references were refreshed with the same
current simulator binary.

DR_EVT was measured in five uninterrupted runs using the same Release binary
and Linux Docker environment as every other DR_EVT comparison on these pages.
One untimed run warmed the filesystem caches. Every measured schedule and
resource trace matches the published result.

| DR_EVT process timing | Wall time |
|---|---:|
| Median | 0.036436 s |
| Minimum | 0.035888 s |
| Maximum | 0.037544 s |

These timings include process launch, trace input, simulation, output, and exit;
Python parsing, audits, plotting, and builds are excluded. Slurm's native
controller/adapter measurement includes RPCs and observation, so it measures
a different amount of work even though both run in Docker on the Apple M3 Max.

[Common DR_EVT timing record](../_static/scheduler-validation/performance/performance.json)
· [Timing script](../_static/scheduler-validation/performance/measure_dr_evt.py)
· [Native performance record](../_static/scheduler-validation/slurm/native-performance.json).
The [earlier macOS timing record](../_static/scheduler-validation/slurm/performance.json)
is retained as historical evidence.

## Divergence analysis

### Sources of divergence and corrections

Two DR_EVT fixes resolved the original license-on differences: retaining the
next backfill tick after jobs start, and checking capacity at a candidate's
exact end time.

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

```{toctree}
:maxdepth: 1

SLURM_LICENSE_OFF_DIAGNOSTICS
```

## Evidence and reproduction

The [Slurm harness](https://github.com/LLNL/dr_evt/blob/main/validation/slurm/README.md)
provides setup and commands, source revisions, clock and synchronization hooks,
workload conversion, and audits for both license-planning settings. The native
reference uses Slurm `17abb848e` (26.11.0-0rc2). DR_EVT revision `d545578` is
identified by its [source hashes](../_static/scheduler-validation/slurm/dr_evt-sources.json).

The [common timing folder](../_static/scheduler-validation/performance/README.txt)
provides the script, benchmark inputs, build details, and Docker environment.

Published figure folders contain scripts, inputs, metrics, and provenance:

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
python3 plot.py plot-input.json
```

For the control folder, run `python3 plot_timeline.py . --case pooled` or
`python3 plot_timeline.py . --case protection`. For the divergence folder,
run `python3 plot_endpoint_comparison.py endpoint-comparison.json`.

[cpp-fcfsperiodichybridcircularscheduler]: https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4N6dr_evt35FCFSPeriodicHybridCircularSchedulerE
