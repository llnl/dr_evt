# Optimization History

These measurements explain earlier optimization choices. They describe the
code and machines used at the time; see [Performance Analysis](../PERFORMANCE_ANALYSIS.md)
for measurements of the current code.

## Circular EASY scheduler profile

An October 2026 Linux `perf` capture used the first 50,000 jobs of the June
2022 RIKEN/Fugaku trace, actual runtimes, 142,167 nodes, and a circular FCFS/EASY
queue. The Release build ran on an Intel Xeon Platinum 8480+ at LLNL Dane.
About 3,000 cycle samples were collected over 3.44 seconds with no lost samples.

| Work | Share of all sampled cycles |
| --- | ---: |
| Copy and destroy the temporary running-job tree | 45.96% |
| Calculate the FCFS reservation | 36.27% |
| Other scheduler work | 2.18% |

Reservation calculation repeatedly traversed running jobs and sorted their
completion events. Together with the map copy, it dominated this capture.
This motivated avoiding ownership copies, reducing repeated release-order
work, and reusing temporary storage. These are historical findings, not a
profile of the current implementation. Inclusive costs overlap with allocator
and standard-library costs and must not be added to them.

## Fixed periodic candidate-prefix selection

Relative to `dae396c`, the scheduler stopped recounting candidate prefixes
when no entries had been removed, and skipped prefix restriction when the
candidate limit already covered all eligible jobs. Removed entries, eligibility,
and compaction kept their original behavior.

A macOS arm64 Release benchmark used a four-node job running for 200,000
seconds followed by 10,000 one-second jobs, one-second dispatch ticks, and a
10,000-job candidate limit. After one warmup, five alternating before/after
pairs produced these median whole-process times:

| Dispatch mode | Before | After |
| --- | ---: | ---: |
| FCFS | 1.107 s | 0.072 s |
| Conservative | 1.121 s | 0.081 s |
| Conservative with arrival notifications | 1.120 s | 0.082 s |

The saturated queue exposed repeated prefix scans. A mixed 20,000-job workload
showed no material improvement. All compared schedules were byte-identical.
The large speedups apply to that repeated-scan workload, not all simulations.

## Fixed periodic reservation-search bounds

Relative to `66dbc21`, a per-pass cache reused earliest-fit answers as lower
bounds while reservations reduced available capacity. It rechecked successful
fits and cleared bounds when a reservation update invalidated the assumption.
The cache targets conservative fixed dispatch with large prefixes and repeated
node-count/duration shapes; other configurations retain the ordinary planner.

On macOS arm64, one warmup and three alternating measured pairs gave:

| Conservative workload | Before | After |
| --- | ---: | ---: |
| Mixed 20,000 jobs, fixed timer | 9.359 s | 4.261 s |
| Same workload with between-tick arrivals | 11.742 s | 5.354 s |
| 8,000 jobs with many distinct request shapes | 13.561 s | 13.736 s |

The mixed workload used seed 2718, four submissions per second, 1–8 nodes,
1–30-second limits, 128 nodes, one-second ticks, and a 10,000-job candidate
limit. The distinct-shape workload used 1–1,024 nodes on 8,192 nodes.
Schedules were byte-identical. Roughly 2.2× improvement with repeated shapes
and no benefit with distinct shapes motivated the selective cache path.

The original captures and temporary benchmark files are not required for
current measurements. Historical source changes can be inspected through Git;
use the current page's reproduction instructions for new timings.
