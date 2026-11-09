# Cached start API: before/after performance comparison

**Finding:** EASY is unchanged within measurement noise. Bounded hybrid scheduling is about **5% slower** in longer runs. The new cached query is **5.7–7.2× faster** than fresh status forecasting for the tested queues. All ten complete-simulation cases produced byte-identical schedules and resource traces.

## What was compared

- Before: committed revision `6a21cb60e5a7671beac694a93fad7af52596a6ce`.
- After: that revision plus the uncommitted cached-start API implementation, captured in `measured-source.diff.gz`. New test sources do not affect the simulator target.
- Native ARM64 macOS 26.7.2; Apple Clang; Release `-O3 -DNDEBUG`, C++20, frame pointers, debug symbols. Both builds use the same local dependencies; gRPC/Protobuf and Ser20 are enabled, Python bindings and MPI are excluded from these benchmark builds.
- Builds are isolated under `/private/tmp/dr_evt-plan-profile-20261109`; the working-tree implementation was not changed during measurement.
- Measurements are sequential, with one warmup for each binary and workload and alternating AB/BA execution order. No builds or profiling run concurrently with timed benchmarks.
- Fifteen measured runs per complete-simulation case; five per longer simulation loop; nine per API query case. Tables report medians. Profiling is separate from timing.

## Longer simulation runs

Each process runs 50 complete simulations of the 10,000-job scale trace, using 795 nodes, circular queue, Q=64, R=32, and requested-limit runtimes. Times include trace parsing and scheduling and exclude final CLI CSV export. The workload starts exactly 500,000 jobs in every measured process.

| Policy | Before wall (s) | After wall (s) | Wall change | Before user CPU (s) | After user CPU (s) | CPU change | Peak RSS before/after (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| easy | 1.5251 | 1.5255 | +0.03% | 1.4628 | 1.4612 | -0.11% | 15.73 / 15.66 |
| hybrid | 4.2113 | 4.4246 | +5.06% | 4.1472 | 4.3551 | +5.01% | 15.81 / 15.98 |

The five hybrid wall times range from 4.206–4.232 s before and 4.399–4.437 s after. Their separation supports a repeatable slowdown on this workload. EASY ranges overlap. These are local-machine results, not a bound on every scheduler configuration.

## Complete simulator runs

Times include process launch, input parsing, simulation, schedule/resource CSV output, and exit. Published validation cases have 2,000 jobs. Scale EASY/hybrid cases have 10,000 jobs; scale conservative has 2,000 jobs and Q=64. All outputs match between builds; the six published cases also pass their existing schedule, capacity, and duration audits.

| Workload | Before wall (ms) | After wall (ms) | Wall change | User CPU change | Peak RSS change (KiB) |
| --- | ---: | ---: | ---: | ---: | ---: |
| fiction-fcfs | 68.79 | 67.26 | -2.22% | +0.26% | -144 |
| fiction-hybrid | 116.31 | 113.22 | -2.66% | +0.95% | -16 |
| slurm-protected | 84.56 | 89.12 | +5.39% | +1.85% | -16 |
| slurm-no-reservations | 60.13 | 56.26 | -6.45% | +0.20% | +0 |
| openpbs-fcfs | 120.46 | 121.12 | +0.54% | +0.62% | -80 |
| openpbs-grouped | 271.31 | 273.99 | +0.99% | +0.59% | +32 |
| scale-easy-circular | 86.30 | 85.96 | -0.39% | -0.90% | -96 |
| scale-easy-deque | 84.71 | 84.40 | -0.37% | -0.35% | -64 |
| scale-hybrid-circular | 140.05 | 144.34 | +3.06% | +3.00% | -32 |
| scale-conservative-circular | 75.68 | 73.09 | -3.43% | +4.55% | +32 |

Short processes show launch and OS scheduling noise: wall-clock changes range from approximately −6% to +5%, including modes with no cached projections. Use the longer runs above to judge the hybrid scheduling cost.

## Query cost

Hybrid R=32/Q=64, 100 nodes: one running job occupies 99 nodes until t=1000; every waiting job requests all 100 nodes for 10 seconds. One live scheduling wake populates the cache. Setup and live planning are excluded from the measured query loop. Query results are consumed and checked for the expected count.

| Waiting jobs | Fresh statuses before (µs/query) | Fresh statuses after (µs/query) | Cached starts after (µs/query) | Cached speedup over fresh after |
| ---: | ---: | ---: | ---: | ---: |
| 1,000 | 64.18 | 62.59 | 11.03 | 5.67× |
| 10,000 | 833.73 | 843.05 | 116.61 | 7.23× |

The cached API provides different information: known live-planner starts plus unknown entries for jobs the planner did not examine. Fresh status forecasting estimates the full requested queue and includes lifecycle information. This is a query-cost comparison, not equivalent-result substitution. The unchanged fresh API varies by −2.5% at 1,000 jobs and +1.1% at 10,000 jobs.

## CPU profile and memory

Four five-second `sample` profiles use repeated EASY/hybrid simulations in separate processes, at one-millisecond sampling intervals. Profiles were collected outside the sandbox because it prevents process inspection. Startup samples are present, so percentages are approximate.

- Existing hot paths remain scheduler scanning, reservation timeline construction, reservation placement, and vector insertion/sorting.
- Hybrid after: `record_planned_start()` accounts for 176 of 4,234 top-of-stack samples (4.16%). EASY after: 9 of 3,919 (0.23%). The hybrid recording helper appears as an out-of-line call in this build. This is the clearest new hot path and a candidate for a later focused optimization.
- The profiles support the measured hybrid overhead; they do not precisely assign every changed CPU cycle. Some compiler-generated hybrid helper frames are unresolved by the native sampler.
- A compiled size probe reports **24 bytes per cached record**, a **24-byte vector object**, and a **16-byte optional plan timestamp**: 40 bytes of fixed scheduler storage plus vector capacity × 24 bytes. Queue entries receive no new fields. The previous documentation statement of 16 bytes per record was incorrect and has been corrected.
- The longer hybrid run has a median peak RSS increase of 176 KiB; EASY decreases by 80 KiB. RSS includes allocator granularity, code pages, and process variation, so these differences are not precise measurements of the cache allocation.

## Evidence and repeatability

A subsequent [inline experiment](INLINE_OPTIMIZATION.md) reduced hybrid overhead. The original after binaries are preserved in `after-original`; `after-build` now contains that optimized candidate. The original report tables and archived measurements describe the initial implementation.

- `performance.json`, `core.json`, and `queries.json`: all individual measurements, medians, ranges, input hashes, and output hashes.
- `profiles/`: raw CPU profiles; `before-compiler-flags.txt` and `after-compiler-flags.txt`: actual library compiler flags.
- `measured-source.diff.gz`, configure options, and build/configure logs record the measured implementation and build provenance.
- `compare.py` and `compare_queries.py` accept `--root` and `--repo` as applicable. The longer-loop and profiling scripts use the recorded temporary root. The two C++ drivers are included; compile each against its matching snapshot headers and library, defining `HAS_PLANNED_STARTS` only for the after query driver.
- Full run commands and CSV files remain under `/private/tmp/dr_evt-plan-profile-20261109/{measurements,core-measurements,query-measurements}`. Binaries and source snapshots also remain there.

```bash
/Library/Developer/CommandLineTools/usr/bin/python3.9 compare.py \
  --root /private/tmp/dr_evt-plan-profile-20261109 \
  --repo /Users/yeom2/work/dr_evt --runs 15
/Library/Developer/CommandLineTools/usr/bin/python3.9 compare_queries.py \
  --root /private/tmp/dr_evt-plan-profile-20261109 --runs 9
```

Captured patch files are gzip-compressed with their original bytes preserved. Generated logs and profiles have trailing whitespace normalized for repository storage.
