# Scheduling Validation

DR_EVT and Fiction with Flux Core's `sched-simple` produce identical schedules
for 2,000 recorded Lassen jobs under FCFS without backfill. Hybrid backfill
with Fluxion produces close resource-use curves but different job starts.
A four-job example below shows how reservations of particular nodes can cause
an earlier backfill start in DR_EVT's node-count model.
A native Slurm comparison uses pooled license capacity to validate periodic
backfill against DR_EVT’s abstract resource model, with main-scheduler
throttling disabled.

These comparisons validate the stated workloads and policy configurations.
They do not establish equivalence to a production scheduler's full behavior.
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

Both schedulers use equal priorities and FCFS order. System latency and node
release delay are disabled, and the planning horizon is unlimited. Fair share,
usage accounting, preemption, GPUs, and heterogeneous topology are excluded.
Every DR_EVT run in the Fiction comparisons uses `--per_pass_accounting`
to select the event-driven placement implementation used to model Flux's
scheduling passes. Immediate starts and successful reservations count toward Q,
and Q/R reset for each pass. These runs use the default resource-time
reservation rule (`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`).

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
It uses a simple node/core resource pool and assigns concrete resources when
jobs start. This simplifies resource matching, but does not provide pooled-count
backfill: `sched-simple` has no backfill.
Its forecast calculates estimated starts in a copied resource pool, without
protecting future nodes in the actual scheduling state.

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

The areas under the resource curves are identical, and all **2,920 native
snapshots** pass capacity and ownership checks. The area between the curves
is **2,947,190.112 node-seconds**, or **2.6612%** of the shared capacity-time
window. The schedules differ; the cause of every recorded-trace difference
has not been isolated.

| Metric | dr_evt | fiction with Fluxion |
|---|---:|---:|
| Mean waiting time | 10.1647 h | 10.5460 h |
| Makespan | 40.2895 h | 40.6922 h |
| Area under resource curve | 81,305,711.274 node-seconds | 81,305,711.274 node-seconds |
| Utilization | 74.1489% | 73.4151% |

Utilization is `100 × resource area / (nodes × makespan)`, using each run's
makespan. The area between curves integrates the absolute difference in
allocated-node counts over a common observation window. Its percentage divides
that area by `nodes × shared window duration`.
Equal resource areas alone cannot rule out jobs claiming the same node; the
native ownership checks test that separately.

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

Once a scheduling decision differs, subsequent queue and resource states can
differ too; aggregate curves and waiting metrics help assess the overall effect.

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

The matching controls start D exactly at the releasing job's completion.
Together with the assignment controls, this identifies a reservation-placement
cause for this reproducer. It does not attribute every Lassen difference to
that cause or measure all possible RPC-ordering effects.
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

Slurm's `bf_licenses` option lets backfill plan the future availability of
licenses and protect those counts for waiting jobs. Ordinary global licenses
are integer counts with no individual token identities. We use them as an
abstract resource pool: `Licenses=capacity:756`, with each job requesting its
original node count as `capacity` licenses. This represents the same pooled
node-count capacity that DR_EVT uses while retaining Slurm's native backfill.

Each job also requests one private dummy compute node. There are 2,000 such
nodes, so physical node binding does not limit competing jobs. The
`select/cons_tres` plugin supports this license-aware planning. With
`bf_licenses` enabled, future license reservations protect the pooled capacity;
with it disabled, current license limits still apply, but backfill does not
protect future license capacity. Periodic backfill remains enabled in both
cases. This gives a comparison using an abstract resource model without
removing backfill as `sched-simple` does.

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
dispatch with periodic backfill. Arrivals and completions trigger main FCFS
dispatch; timer ticks run backfill. These settings select the intended
scheduling models; matching option values do not establish identical policies.

Native state and license snapshots check capacity, private node ownership,
exact runtimes, and completed jobs. Independent audits also verify that both
schedulers' resource traces agree with their job records.

### Lassen results with bf_licenses on

All **2,000 jobs** and **7,527 native snapshots** pass the audits. Both
schedulers use **81,309,892 node-seconds**. There are **1,524 differing start
times**, with a largest absolute difference of **19,380 seconds**. The area
between resource curves is **1,346,976 node-seconds**, or **1.2166%** of the
shared capacity-time window. Aggregate metrics are close; individual schedules
are different.

| Metric | DR_EVT | Slurm, bf_licenses on |
|---|---:|---:|
| Mean waiting time | 10.4462 h | 10.3973 h |
| Makespan | 40.6817 h | 40.6236 h |
| Area under resource curve | 81,309,892 node-seconds | 81,309,892 node-seconds |
| Utilization | 73.4379% | 73.5429% |

:::{figure} ../_static/scheduler-validation/slurm/comparison-metrics.svg
:alt: Six-panel comparison with bf_licenses enabled, showing close waiting-time distributions, utilization, makespan, and resource area, with differing individual start times.
:width: 100%

Native Slurm with license-aware backfill planning and DR_EVT's periodic hybrid model.
:::

[Six-panel PDF](../_static/scheduler-validation/slurm/comparison-metrics.pdf)
· [Resource-curve overview](../_static/scheduler-validation/slurm/comparison.pdf).

### Lassen results with bf_licenses off

The second run uses identical inputs, native binaries, and capacity limits,
with `bf_licenses` disabled. Its DR_EVT reference schedule is identical to the
one above. All **2,000 jobs** and **6,786 native snapshots** pass the audits.
There are **1,943 differing start times** from DR_EVT, with a largest absolute
difference of **110,863 seconds**. The area between curves is
**12,123,504 node-seconds**, or **10.9498%** of the shared capacity-time window.

| Metric | DR_EVT | Slurm, bf_licenses off |
|---|---:|---:|
| Mean waiting time | 10.4462 h | 3.2904 h |
| Makespan | 40.6817 h | 33.3911 h |
| Area under resource curve | 81,309,892 node-seconds | 81,309,892 node-seconds |
| Utilization | 73.4379% | 89.4722% |
| Maximum waiting time | 18.3186 h | 30.8106 h |

Disabling future license protection lets more jobs start early, reducing
aggregate waiting and makespan in this workload. It also permits later jobs
to delay earlier jobs: maximum waiting time increases. These results exercise
a different reservation policy for the pooled resource.

:::{figure} ../_static/scheduler-validation/slurm/bf-licenses-off/comparison-metrics.svg
:alt: Six-panel comparison with bf_licenses disabled, showing lower mean wait and makespan, higher utilization, and a longer maximum wait than DR_EVT.
:width: 100%

Native Slurm backfill with current license limits but no future license reservations.
:::

[Six-panel PDF](../_static/scheduler-validation/slurm/bf-licenses-off/comparison-metrics.pdf)
· [Combined on/off metrics](../_static/scheduler-validation/slurm/license-planning-metrics.csv).

### Four-job scenarios: matching and different reservations

Both scenarios use four node equivalents, FCFS order, simultaneous arrivals
at zero, and actual runtimes equal to time limits. A requests two nodes for
120 seconds, B two for 180 seconds, and D one for 120 seconds.

In the **original scenario**, C requests **three nodes for 60 seconds**.
Both Slurm settings and DR_EVT start A/B at zero, D at **120**, and C at
**180 seconds**. D can continue through C's reservation: their combined
requirement is four nodes. The original scenario therefore matches with
license planning either enabled or disabled.

:::{figure} ../_static/scheduler-validation/slurm/controls/four-job-node-assignments.svg
:alt: Original four-job workload with C requesting three nodes: Slurm license planning on, Slurm off, and DR_EVT all place D at 120 and C at 180 seconds.
:width: 100%

Original scenario: D and C coexist within the four-node capacity.
:::

The **reservation-protection scenario** changes only C to request **all four
nodes**, still for 60 seconds. With `bf_licenses` on, Slurm and DR_EVT protect
C's **180-second** start and postpone D until **240 seconds**. With planning
off, Slurm starts D at **120 seconds**, consuming a license across C's planned
start and delaying C to **240 seconds**.

:::{figure} ../_static/scheduler-validation/slurm/controls/reservation-protection-node-assignments.svg
:alt: Changing C to four nodes makes Slurm with bf_licenses on match DR_EVT: C starts at 180 and D at 240; with planning off, D starts at 120 and delays C to 240.
:width: 100%

Changing C's size isolates the protection supplied by future license reservations.
:::

The node rows show **feasible assignments to node-equivalent slots**. License
counts have no individual node identities, and DR_EVT does not emit node IDs;
these rows illustrate pooled placement rather than recorded physical node IDs.
Both figures retain workload tables, plotting scripts, and exact plotting data.

### Remaining divergence with bf_licenses on

The first difference in chronological scheduling order is **job 243**:
Slurm starts it at **5,820 seconds**, DR_EVT at **5,850 seconds**. Before that
instant, the two runs have identical starts and completions. Job 201, previously
highlighted by input order, is a later difference at 6,870 versus 6,930 seconds.

At the **5,790-second** tick, both schedulers start jobs **173 and 218**.
Job 243 is initially queue position **66**, outside Q=64. Those two starts
move it to position **64**, making it eligible for the next tick. There are
no new arrivals or completions before **5,820 seconds**.

DR_EVT's simulation driver clears `m_periodic_backfill_dirty` before the
5,790-second pass and does not set it again for jobs started by that pass.
It therefore skips backfill at 5,820. Slurm's `_more_work()` accepts job-state
updates from the preceding backfill cycle and retries the changed queue,
starting job 243. Job **236 completes at 5,849 seconds**, which marks DR_EVT's
state dirty; its 5,850-second tick then starts job 243.

An independent resource-time replay reproduces DR_EVT's two starts at 5,790
and verifies that a DR_EVT pass at 5,820 would admit job 243. The recorded
native log and queue reconstruction support this **driver wake-up cause** for
the first divergence. The causes of every later difference remain unisolated.
The performance rerun below uses the same implementation.

:::{figure} ../_static/scheduler-validation/slurm/divergence/first-divergence.svg
:alt: Job 243 moves from queue position 66 to 64 after two starts. Slurm retries at 5820 seconds; DR_EVT skips that tick and starts the job at 5850 after a completion at 5849.
:width: 100%

A changed bounded queue needs another backfill tick even without a new arrival or completion.
:::

[Explanatory PDF](../_static/scheduler-validation/slurm/divergence/first-divergence.pdf)
· [Saved diagnostic evidence](../_static/scheduler-validation/slurm/divergence/first-divergence-data.json).

### Uninterrupted license-enabled performance run

A fresh 2,000-job run with `bf_licenses` on used the same native binaries and
reproduced every start and completion from the enabled comparison above.
All 2,000 jobs and 7,527 snapshots pass the independent audit. Logging stayed
at `info` throughout; the run had no restarts or external changes to the
scheduler or logging configuration.

| Timed phase | Elapsed wall time |
|---|---:|
| Native Slurm controller and validation adapter | 716.8365 s |
| DR_EVT simulator invocation | 0.0690 s |
| Complete validation process | 728.6231 s |

The native phase includes controller startup, virtual node registration,
allocation/completion RPCs, job-state observation, real-time synchronization
waits, and snapshot writes. The complete process also includes the DR_EVT
comparison, figure generation, and native shutdown. These are single-run
timings of the instrumented validation workflow; virtual job durations do
not consume matching wall-clock time. The earlier enabled run changed log
verbosity partway through and is not used for this performance measurement.

[Performance record](../_static/scheduler-validation/slurm/performance.json)
· [Timing script](../_static/scheduler-validation/slurm/run_performance.py).

## Evidence and reproduction

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
  [divergence plotting script](../_static/scheduler-validation/slurm/divergence/plot_first_divergence.py).

With NumPy and Matplotlib installed, download a complete figure folder and run:

```sh
python3 plot_comparison.py --plot-input plot-input.json
```

For either Slurm Lassen figure folder, run `python3 plot.py plot-input.json`.
For the control folder, run `python3 plot_timeline.py . --case pooled` and
`python3 plot_timeline.py . --case protection`. For the divergence folder,
run `python3 plot_first_divergence.py first-divergence-data.json`.

The Fiction comparisons use DR_EVT revision `57c3267`, Flux Core `d0309fcf0`,
Fluxion `9eba598b`, and Fiction `346cbd65`. Machine-readable evidence is in
[validation/flux/evidence](https://github.com/LLNL/dr_evt/tree/main/validation/flux/evidence).
The native Slurm comparison uses DR_EVT `40830a8` and Slurm `17abb848e`
(26.11.0-0rc2). The [Slurm harness](https://github.com/LLNL/dr_evt/blob/main/validation/slurm/README.md)
records source revisions, clock and synchronization hooks, workload conversion,
audits, and both license-planning settings. Experiment results and plotting
artifacts are stored first under `validation/slurm/`; published figures are
copied from those audited results.
