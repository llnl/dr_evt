# Performance Analysis

These measurements use DR_EVT revision `f680afd`, including protected-only
start-prediction cache reuse. They measure complete simulator runs; prediction
queries are covered in [Scheduling Predictions and Reservations](SCHEDULING_PREDICTIONS.md#performance-and-accuracy-comparison).

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
