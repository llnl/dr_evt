# Scheduling Validation

DR_EVT and Fiction with Flux Core's `sched-simple` produce identical schedules
for 2,000 recorded Lassen jobs under FCFS without backfill. Hybrid backfill
with Fluxion produces close resource-use curves but different job starts.
A four-job example below shows how reservations of particular nodes can cause
an earlier backfill start in DR_EVT's node-count model.

These comparisons validate the stated workloads and policy configurations.
They do not establish equivalence to a production scheduler's full behavior.
See [Scheduling Loops](SCHEDULER_LOOPS.md) and
[Realism and Known Gaps](REALISM_AND_GAPS.md) for the supported policy sets.

## Workload and configuration

Both recorded-workload comparisons use the first **2,000 chronological jobs**
from `lassen-756-01.csv`, starting with an empty cluster of **756 nodes**.
Each node has one core and jobs allocate whole nodes. Arrivals, time limits,
and actual durations come from the trace. The selected source records span
**August 19–21, 2020 UTC**, from the earliest submission to the latest recorded
completion; the simulated schedules can finish at different times.

Both schedulers use equal priorities and FCFS order. System latency and node
release delay are disabled, and the planning horizon is unlimited. Fair share,
usage accounting, preemption, GPUs, and heterogeneous topology are excluded.
Every DR_EVT run uses `--per_pass_accounting` and the default resource-time
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
:alt: Closely following dr_evt and Fiction allocated-node curves for hybrid backfill with reservation depth 32 and queue depth 64, with a zoom around the first difference and the signed node difference below.
:width: 100%

Hybrid backfill: similar resource use with differences in when nodes are occupied.
:::

:::{figure} ../_static/scheduler-validation/fluxion/comparison-metrics.svg
:alt: Waiting-time and utilization comparison for the hybrid runs, including fiction waiting time minus dr_evt waiting time; Fiction has a longer makespan and lower utilization despite equal resource areas.
:width: 100%

Hybrid backfill: aggregate metrics and the distribution of waiting-time differences.
:::

Two additional 2,000-job synthetic traces use four nodes and the same R=32/Q=64.
Their resource-curve differences are smaller:

| Synthetic workload | Mean wait: dr_evt / fiction | Area between curves | Shared-window percentage |
|---|---:|---:|---:|
| Bursts of 24 jobs | 287.960 / 288.955 s | 1,420 node-seconds | 0.1069% |
| Bursts of 48 jobs | 608.290 / 608.080 s | 1,940 node-seconds | 0.2940% |

In each synthetic comparison, resource area, makespan, and utilization are
equal. With backfill disabled, both synthetic traces match completely.
Once a scheduling decision differs, subsequent queue and resource states can
differ too; aggregate curves and waiting metrics help assess the overall effect.

## Minimal start-time divergence

The following reproducer has **four jobs on four nodes total**, one core per
node. Jobs use FCFS order, durations equal time limits, Q=2048, and no horizon
or simulated release delay. The queue cap exceeds the entire workload, so
per-pass resets do not explain this difference.

:::{figure} ../_static/scheduler-validation/four-job-backfill-divergence.png
:alt: Four-job example using low-node-ID-first matching. Fluxion reserves nodes 0, 1, and 2 for head job C at 80 seconds, so backfill job D waits until 80; a feasible dr_evt count-based schedule starts D at 70 while preserving C's start at 80.
:width: 100%

Specific-node reservations delay D by 10 seconds while preserving C's start.
:::

[Download the updated slide](../_static/scheduler-validation/four-job-backfill-divergence.pptx).

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

## Evidence and reproduction

The [container harness](https://github.com/LLNL/dr_evt/blob/main/validation/flux/README.md)
contains commands for both native schedulers, the recorded trace, synthetic
workloads, resource audits, and the minimal reproducer. Raw Lassen source logs
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

With NumPy and Matplotlib installed, download a complete figure folder and run:

```sh
python3 plot_comparison.py --plot-input plot-input.json
```

The checked results use DR_EVT revision `57c3267`, Flux Core `d0309fcf0`,
Fluxion `9eba598b`, and Fiction `346cbd65`. Machine-readable evidence is in
[validation/flux/evidence](https://github.com/LLNL/dr_evt/tree/main/validation/flux/evidence).
