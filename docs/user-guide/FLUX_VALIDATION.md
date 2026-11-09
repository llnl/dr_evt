# Flux Simulation Validation

Fiction drives simulated arrivals and completions through native Flux schedulers.
FCFS without backfill matches all 2,000 Lassen job starts and completions.
Hybrid backfill has different starts; a four-job example isolates the effect
of reservations tied to specific nodes.

## Workload and configuration

| Fiction experiment | Scheduler backend |
|---|---|
| FCFS without backfill | `sched-simple`; Fluxion is unloaded |
| Hybrid backfill | Fluxion |

Both recorded-workload comparisons with Fiction use **2,000 jobs** from
[Lassen's job trace](https://github.com/LLNL/LAST), submitted from
**2020-08-19 04:29:39 PDT** through **2020-08-19 15:38:34 PDT**, starting
with an empty cluster partition (`pbatch`) of **756 nodes**.
Each node has one core and jobs allocate whole nodes. Arrivals, time limits,
and actual durations come from the trace. The latest recorded completion is
**2020-08-20 21:06:09.138 PDT**; simulated schedules can finish at different times.

Both schedulers use equal priorities, FCFS order, no system latency or node
release delay, and an unlimited planning horizon. Fair share, usage accounting,
preemption, GPUs, and heterogeneous topology are excluded.
DR_EVT uses `--per_pass_accounting` to select the event-driven placement
implementation: immediate starts and reservations
count toward Q, and Q/R reset each pass. The resource-time reservation rule is
`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`.

Fiction fires arrivals and completions in simulation-time order. After each
event batch, it waits for scheduler decisions and resulting starts before
advancing time. Starts determine completion events at `start + actual duration`.
The native scheduler owns the waiting queue and resource allocations.
Read-only synchronization and resource-snapshot extensions support this handshake
without changing allocation, scheduling, or backfill rules.

## Results

### FCFS without backfill: identical sched-simple run

Both engines use FCFS without backfill, with Q=64. For `sched-simple`, Q bounds
outstanding allocation requests, rather than jobs examined per pass. It stops
at the first blocked queue head; jobs behind that head cannot start early.
It uses a simple node/core pool and assigns concrete resources at job start.
`sched-simple` estimates future starts in a copied pool without protecting
future nodes in scheduling state.

All **2,000 start and completion times match at millisecond output precision**,
as do the resource curves and waiting metrics. All **2,925 native snapshots**
pass capacity and node-ownership checks.

| Metric | DR_EVT | Fiction |
|---|---:|---:|
| Mean waiting time | 11.4116 h | 11.4116 h |
| Makespan | 41.1710 h | 41.1710 h |
| Area under resource curve | 81,305,711.274 node-seconds | 81,305,711.274 node-seconds |
| Utilization | 72.5613% | 72.5613% |
| Process runtime | 0.020419 s (median of 5 runs) | 145.849 s |

DR_EVT times are medians of five runs in the common Linux Docker environment
on an Apple M3 Max. Timing includes process launch, input, simulation, output,
and exit. Fiction's single recorded timing also includes Flux startup,
synchronization, resource snapshots, and postprocessing. Builds and separate
audits are excluded. The [common timing record](../_static/scheduler-validation/performance/performance.json)
identifies the current DR_EVT build and all repeats.

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

### Hybrid backfill with Fiction

Fiction runs Fluxion's hybrid-backfill scheduler for the same 2,000 Lassen
jobs, with **R=32,Q=64**. Fluxion uses
**`lonodex` (low-node-ID-first)** resource matching and reserves particular
nodes. DR_EVT protects available node counts over time; it does not reproduce
that resource matching or those node-specific reservations.

Both runs use the same resource area, and all **2,920 native snapshots** pass
capacity and ownership checks. The area between curves is **2,947,190.112
node-seconds**, or **2.6612%** of the shared capacity-time window. The cause of
every trace difference has not been isolated.

| Metric | DR_EVT | Fiction |
|---|---:|---:|
| Mean waiting time | 10.1647 h | 10.5460 h |
| Makespan | 40.2895 h | 40.6922 h |
| Area under resource curve | 81,305,711.274 node-seconds | 81,305,711.274 node-seconds |
| Utilization | 74.1489% | 73.4151% |
| Process runtime | 0.061328 s (median of 5 runs) | 376.8174 s |

The fresh native hybrid timing run reproduced the published schedule and
passed all 2,920 snapshot audits. Its [timing record](../_static/scheduler-validation/performance/native-hybrid.json)
includes the native build, configuration, and verification. DR_EVT used the
same common binary and timing method as the FCFS comparison.

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

## Divergence analysis

### Minimal reproducer for backfill start-time differences

The following reproducer has **four jobs on four nodes total**, one core per
node. Jobs use FCFS order, durations equal time limits, Q=2048, and no horizon
or simulated release delay. The queue cap exceeds the entire workload, so
per-pass resets do not explain this difference.

:::{figure} ../_static/scheduler-validation/four-job-backfill-divergence.png
:alt: Four-job example using low-node-ID-first matching. Fiction reserves nodes 0, 1, and 2 for head job C at 80 seconds, so backfill job D waits until 80; a feasible dr_evt count-based schedule starts D at 70 while preserving C's start at 80.
:width: 100%

Specific-node reservations delay D by 10 seconds while preserving C's start.
:::

With `lonodex`, A occupies nodes 0,1 until 70; B occupies nodes 2,3 until 80.
C protects nodes 0,1,2 for 80–90. At 70 the free nodes 0,1 are both protected,
so Fiction cannot start D there for its 50-second duration. D waits until node
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

## Evidence and reproduction

The [Fiction harness](https://github.com/LLNL/dr_evt/blob/main/validation/flux/README.md)
provides setup, replay commands, audits, and the four-job reproducer.
The recorded revisions are DR_EVT `57c3267`, Fiction `346cbd65`, and Flux Core
`d0309fcf0`. Hybrid backfill also uses Fluxion `9eba598b`.
[Machine-readable evidence](https://github.com/LLNL/dr_evt/tree/main/validation/flux/evidence)
records the runs.

The [common timing folder](../_static/scheduler-validation/performance/README.txt)
contains all six DR_EVT measurement cases, the script, build details, and
Docker environment record. All repeated schedules match the recorded results.

The figure folders contain scripts, inputs, metrics, and provenance:

- Fiction, FCFS without backfill: [plotting script](../_static/scheduler-validation/sched-simple/plot_comparison.py),
  [inputs](../_static/scheduler-validation/sched-simple/plot-input.json),
  [numbers](../_static/scheduler-validation/sched-simple/plot-data.json),
  [summary](../_static/scheduler-validation/sched-simple/plot-summary.csv),
  [provenance](../_static/scheduler-validation/sched-simple/provenance.json).
- Fiction, hybrid backfill: [plotting script](../_static/scheduler-validation/fluxion/plot_comparison.py),
  [inputs](../_static/scheduler-validation/fluxion/plot-input.json),
  [numbers](../_static/scheduler-validation/fluxion/plot-data.json),
  [summary](../_static/scheduler-validation/fluxion/plot-summary.csv),
  [provenance](../_static/scheduler-validation/fluxion/provenance.json).

With NumPy and Matplotlib installed, download either complete figure folder and run:

```sh
python3 plot_comparison.py --plot-input plot-input.json
```
