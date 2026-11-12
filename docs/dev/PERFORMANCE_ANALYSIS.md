# Performance Analysis

These measurements use DR_EVT revision `f680afd`, including protected-only
start-prediction cache reuse. They measure complete simulator runs. Start-time
prediction query costs are summarized separately below and covered in detail in
[Scheduling Predictions and Reservations](SCHEDULING_PREDICTIONS.md#performance-and-accuracy-comparison).

## Current simulation timings

All six configurations use the 2,000-job validation workload on 756 nodes,
actual execution durations, requested time limits for planning, and the circular
wait queue. Flux cases preserve fractional durations; Slurm and OpenPBS cases
use the published integer-second durations. The policy settings follow the published production-scheduler
validation cases. The names below identify DR_EVT configurations; these are
DR_EVT timings, not measurements of Slurm, Flux, or OpenPBS.

The binary was built with GCC 14.2, Release (`-O3 -DNDEBUG`), and
`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`, in Linux arm64 Docker on an Apple M3 Max.
Serialization, Python bindings, Protobuf, and gRPC were enabled in the build;
the measurements run the simulator CLI. Each case had one warmup followed by
seven sequential measured runs.

| DR_EVT configuration | Median wall time (ms) | Min–max (ms) |
| --- | ---: | ---: |
| Flux-style FCFS, no backfill | 19.67 | 19.60–20.03 |
| Flux-style hybrid, R32/Q64 | 62.38 | 62.08–63.22 |
| Slurm-style protected, 30 s planning | 37.42 | 37.19–37.98 |
| Slurm license-off diagnostic | 11.50 | 11.18–12.30 |
| OpenPBS-style ordinary, Q2000 | 60.15 | 59.76–60.40 |
| OpenPBS-style 1 s event groups, Q2000 | 201.87 | 200.77–205.46 |

Wall time includes process startup, input parsing, scheduling, event handling,
schedule/resource CSV output, and exit. It excludes the build and Python
validation. Every run matched the published job schedule and passed duration,
capacity, and resource-area audits; output hashes matched its warmup.

## What the results show

Reservation planning and queue examination affect total cost. In these runs,
Flux-style bounded hybrid took about 3.2× the time of FCFS without backfill.
The Slurm-style diagnostic without protected reservations was cheaper than
protected periodic planning, but it also changes scheduling behavior and is
not an interchangeable faster configuration.

The OpenPBS-style one-second grouped configuration took about 3.4× the ordinary
configuration. Both examine up to 2,000 jobs; grouping changes projected releases
and scheduling decisions. These are whole-run comparisons, so they do not
isolate the cost of grouping or a single function.

These short runs characterize this workload, build, and machine. Larger queues,
more concurrent jobs, different time-limit errors, dispatch cadence, and output
volume can change the balance. The earlier map-copy and reservation-search
profiles are preserved in [Optimization History](performance-analysis/OPTIMIZATION_HISTORY.md);
their hotspot percentages do not describe the current code.

## Current fixed conservative profile

A separate seeded workload has 20,000 jobs submitted four per second, requesting
1–8 nodes for 1–30 seconds on 128 nodes. Execution uses the time limits,
one-second fixed dispatch, a 10,000-job candidate limit, and the circular queue.
The waiting queue peaked at 10,487 jobs. The ordinary Release binary took a
median **3.862 s** over five measured runs
after one warmup (range 3.856–4.112 s).

A separate GCC 14.2 gprof build of `f680afd` retained `-O3`, used static libraries,
and disabled LTO and identical-code folding. One run collected 372 application
samples at 100 Hz. Its schedule was byte-identical to every ordinary run;
capacity and duration audits passed.

| Work | Sampled application self time |
| --- | ---: |
| Earliest-fit search and cache handling | 67.2% |
| Reservation timeline updates | 26.1% |
| Other work inside the cached scheduling pass | 5.4% |
| Checking whether request shapes can reuse the cache | 0.5% |

Searches and timeline updates still dominate, despite the earlier optimizations.
Further work should target repeated capacity-segment searches and
splitting/updating reservation windows. Prefix selection is a small cost here.
The search row includes cache handling and fit checks; it is not an isolated
cache-overhead measurement.

These shares describe sampled application code, not total process wall time;
shared-library costs are not attributed. Compiler clones are mapped using the
complete symbol table. One short synthetic capture identifies broad targets,
not production-wide percentages or expected speedups. The
[profile summary](https://github.com/LLNL/dr_evt/blob/main/validation/performance/simulation/profile.json)
records the workload, samples, hashes, and build settings.

## Integrated placement bottlenecks

A separate whole-simulator study measured the placement feature variants on a
Dane x86_64 compute node. The Release binary used GCC 13.3.1, disabled optional
serialization/Python/RPC components and observers, and ran every variant
serially. Each result is the median of three measured processes after one
warmup. Timings include parsing, scheduling, events, CSV output, process startup,
and shutdown.

| Workload and policy | No placement optimizations (ms) | Immediate fast path (ms) | Batch seeding (ms) | Scratch reuse (ms) | Boolean fit (ms) | All (ms) | All + coarse filter (ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Contended 10K, EASY | 18,717.60 | 18,699.69 | 18,683.34 | 17,448.99 | 16,814.69 | **16,634.36** | 26,187.42 |
| Contended 10K, HYBRID R32/Q32 | 1,526.48 | 1,524.10 | 1,517.70 | 1,312.84 | 1,525.72 | **1,311.23** | 2,981.35 |
| Seeded low-load 20K, EASY | 300.90 | 106.66 | 174.66 | 300.11 | 299.99 | 102.14 | **100.63** |
| Seeded low-load 20K, HYBRID R32/Q32 | 304.21 | 102.13 | 181.30 | 304.25 | 305.47 | 107.71 | **98.20** |

The two workloads expose different limiting work:

- The seeded 20K trace never creates a future reservation. Its untimed
  diagnostics contain only 20,000 immediate-fit, allocation, and release calls.
  The immediate fast path cuts whole-process time by about 65%, identifying
  avoidable planner/timeline setup as the low-load bottleneck. Scratch reuse and
  boolean fitting have no material effect because their future-query paths do
  not run.
- The contended EASY trace performs 116,697,287 immediate-fit checks. Boolean
  fitting improves the unoptimized run by 10.2%, scratch reuse by 6.8%, and all
  changes together by 11.1%; immediate setup and batch seeding alone are within
  0.2%. Repeated exact bitmap fit work, rather than planner construction, is the
  principal target on this trace.
- The contended HYBRID trace performs 638,058 earliest-fit searches and 629,028
  reservation updates. Scratch reuse accounts for essentially all of the 14.1%
  combined improvement. Repeated future-fit scratch/mask handling and timeline
  reservation work are therefore the next HYBRID targets.
- The coarse capacity prefilter does not reduce the recorded exact operation
  counts. With all other optimizations enabled, it makes the contended EASY run
  57% slower and the HYBRID run 127% slower. It should remain disabled unless a
  different workload demonstrates enough rejected exact queries to repay its
  duplicate accounting cost.

All placement variants produced identical schedule and resource-history hashes
within each case, and every output passed job, duration, and capacity audits.
The aggregate-capacity runs are not equality or speedup baselines for placement:
on the contended trace they make different scheduling decisions.

The study has three measured rounds, and some low-load wall samples contain
multi-second system-noise outliers even though their CPU samples remain near
85–355 ms. Medians support the large feature effects above, but not small
percentage claims. The source tree was dirty because it contained the placement
integration under test. The
[curated measurement record](../../validation/performance/simulation/placement-model/integrated-dane-20261112.json)
retains report hashes, medians, ranges, diagnostics, and binary/input hashes;
the full local reports remain under `results/dane-20261112/`.

## Start-time prediction performance

The current accuracy study queries an event-driven HYBRID R32/Q64 run with
2,000 jobs. Its reported cost is summed mean query time divided by the number of
finite predictions, so methods with different coverage must not be compared as
if they returned identical results.

| Prediction method | Actual-duration coverage | Actual-duration cost (µs/prediction) | Limit-duration coverage | Limit-duration cost (µs/prediction) |
| --- | ---: | ---: | ---: | ---: |
| Cached lookup | 6.13% | **0.138** | 1.58% | 0.420 |
| Forecast without backfill | 100% | 0.258 | 100% | **0.236** |
| Cache-first extension | 100% | 0.271 | 100% | 0.254 |
| Side simulation | 100% | 8.754 | 100% | 6.237 |

Cached lookup is cheapest only because it returns the starts already produced
by scheduling. Forecast and cache-first provide full coverage at similar cost.
Side simulation is about 24–34× more expensive per finite prediction than the
two full-coverage forecast methods because it prepares and runs a checkpoint
continuation; use that cost only when duration estimates and replay fidelity
justify it. Accuracy, not cost alone, determines the choice: with execution
durations equal to time limits, side simulation's own-set MAE is 0.003 minutes,
versus roughly 112–113 minutes for the inexpensive full-coverage methods.

Historical scale microbenchmarks identify two implementation costs behind the
cached API:

- Recording planned starts initially increased a 10K-job bounded-HYBRID loop by
  5.1%. Inlining the recording helper reduced that to about 2.4%; EASY remained
  indistinguishable from its baseline. Recording inside the hybrid planning
  loop is therefore measurable, but no longer the dominant whole-run cost.
- Cached lookup over a 10K-entry queue took 116.61 µs/query while a complete
  forecast took 843.05 µs/query. Sorting only finite results later reduced the
  sparse 64-result cached case from 115.61 to 72.93 µs/query and the zero-result
  case from 91.68 to 45.72 µs/query. The full-coverage case improved only from
  317.09 to 310.37 µs/query. Scanning is still linear, but sorting unknown
  entries was the avoidable bottleneck for sparse results.

These measurements were collected on Apple arm64 with different historical
source snapshots and must not be numerically combined with the Dane placement
study. See the [cached-start report](https://github.com/LLNL/dr_evt/blob/main/validation/performance/estimated-starts/REPORT.md),
[query-sort experiment](https://github.com/LLNL/dr_evt/blob/main/validation/performance/estimated-starts/QUERY_SORT_OPTIMIZATION.md),
and [accuracy/cost methodology](SCHEDULING_PREDICTIONS.md#performance-and-accuracy-comparison)
for hashes, run counts, prediction errors, and reproduction scope.

## Reproduce or collect a new profile

Build an optimized simulator and run the existing audited measurement driver:

```bash
cmake -S . -B build-performance \
  -DCMAKE_BUILD_TYPE=Release \
  -DDR_EVT_LEGACY_RESERVATION_BARRIER=OFF \
  -DDR_EVT_WITH_SER20=ON \
  -DDR_EVT_BUILD_PYTHON=ON \
  -DDR_EVT_ENABLE_PROTOBUF=ON \
  -DDR_EVT_ENABLE_GRPC=ON
cmake --build build-performance -j2
python3 docs/_static/scheduler-validation/performance/measure_dr_evt.py \
  build-performance/simulator --runs 7 \
  --outdir /tmp/dr_evt_performance_run
```

Choose a new output directory for each run. The driver writes local timings and
CSV outputs and checks them against
[`cases.json`](../_static/scheduler-validation/performance/cases.json).
The committed [summary JSON](https://github.com/LLNL/dr_evt/blob/main/validation/performance/simulation/performance.json)
records the measured revision, build settings, binary/input hashes, and results.

See [profile reproduction instructions](https://github.com/LLNL/dr_evt/blob/main/validation/performance/simulation/README.md)
for the seeded workload, gprof build, and symbol-table correction used above.

To identify function costs, use [Linux `perf`](../getting-started/installation.md#linux-perf-profiling-recommended)
or [GNU `gprof`](../getting-started/installation.md#gnu-gprof-profiling) on a longer
workload with the same configuration. Report sampled costs separately from
unprofiled wall time. For queue variants, use the
[wait-queue benchmark](WAIT_QUEUES.md#benchmark-record).

```{toctree}
:maxdepth: 1

performance-analysis/OPTIMIZATION_HISTORY
```
