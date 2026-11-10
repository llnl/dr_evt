# Cached-start performance

The initial cached API added about 5% to bounded hybrid simulation time; EASY was unchanged. Inlining reduced hybrid overhead to about 2.4%. Cached queries were 5.7–7.2× faster than fresh forecasting, but returned fewer predictions. See [the current accuracy comparison](prediction-evaluation/COMPARISON.md) for costs normalized by predicted jobs.

## Method

Baseline: `6a21cb60e5a7671beac694a93fad7af52596a6ce`. The initial candidate was uncommitted; the feature and inline optimization were subsequently committed in `db5edc48047c0725b4c9466676c233a1161580be`. Git retains committed versions, which may differ from intermediate measured candidates.

Native ARM64 macOS, Apple Clang, Release `-O3 -DNDEBUG`, C++20, debug symbols and frame pointers; identical dependencies. One warmup, alternating sequential runs, medians of 15 complete simulations, five longer loops, and nine query loops. Profiling was separate from timing.

## Simulation cost

Longer loops run 50 simulations of 10,000 jobs: 795 nodes, circular queue, Q=64/R=32, requested-limit runtimes. Timing includes parsing and scheduling, excluding CLI export.

| Policy | Before wall (s) | After wall (s) | Wall change | Before user CPU (s) | After user CPU (s) | CPU change | Peak RSS before/after (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| easy | 1.5251 | 1.5255 | +0.03% | 1.4628 | 1.4612 | -0.11% | 15.73 / 15.66 |
| hybrid | 4.2113 | 4.4246 | +5.06% | 4.1472 | 4.3551 | +5.01% | 15.81 / 15.98 |

The hybrid ranges did not overlap; EASY ranges did. Peak RSS changes include allocator and process variation.

Complete runs include launch, parsing, scheduling, CSV export, and exit. Published cases contain 2,000 jobs; scale cases contain 10,000 except conservative (2,000, Q=64). All ten cases produced identical schedules and resource traces.

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

Short-run wall times include launch and OS scheduling noise; use the longer loops to assess scheduling overhead.

## Query cost

Hybrid R=32/Q=64, 100 nodes: one running job occupies 99 nodes until t=1000; waiting jobs request 100 nodes for 10 seconds. Timing excludes setup and planning; results are consumed and response lengths checked.

| Waiting jobs | Fresh statuses before (µs/query) | Fresh statuses after (µs/query) | Cached starts after (µs/query) | Cached speedup over fresh after |
| ---: | ---: | ---: | ---: | ---: |
| 1,000 | 64.18 | 62.59 | 11.03 | 5.67× |
| 10,000 | 833.73 | 843.05 | 116.61 | 7.23× |

Cached lookup leaves unexamined starts unknown; fresh forecasting estimates the full requested queue. These timings compare different coverage levels.

## Optimizations and storage

The original recording helper accounted for about 4.16% of hybrid CPU samples versus 0.23% for EASY. [Inlining](INLINE_OPTIMIZATION.md) reduced its cost. [Sorting only known starts](QUERY_SORT_OPTIMIZATION.md) later reduced sparse-query latency.

Cache storage is 24 bytes per record plus 40 fixed bytes (vector and optional timestamp). Waiting-queue entries receive no additional fields.

## Reproduction

`performance.json`, `core.json`, and `queries.json` retain measurements and hashes. Benchmark drivers and comparison scripts remain alongside this report; some historical scripts require local before/after builds in their configured temporary paths. Source versions are available in Git history. Archived source patches and profiler samples are omitted.

`compare.py` requires `--root`, `--repo`, `--before-revision`, and `--after-revision`. Build the specified commits into `before-build/` and `after-build/` under the root, with `configure-options.json` describing their configuration. The script resolves both commits and hashes their Git diff directly; no `source.diff` file is needed.

For the current four-method evaluation, use [the reproduction instructions](prediction-evaluation/README.md).
