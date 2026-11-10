# Inline cached-start recording

Inlining `record_estimated_start()` reduced bounded hybrid runtime by 2.04%, leaving 2.43% overhead against the pre-feature baseline. EASY was unchanged within measurement noise.

Each process ran 50 simulations of 10,000 jobs: circular queue, 795 nodes, Q=64/R=32, requested-limit runtimes. Results are medians of five alternating runs after one warmup, using identical native Release builds.

| Policy | Pre-feature baseline (s) | Initial cached API (s) | Inline cached API (s) | Improvement over initial | Remaining overhead over baseline |
| --- | ---: | ---: | ---: | ---: | ---: |
| easy | 1.5451 | 1.5453 | 1.5434 | 0.12% | -0.11% |
| hybrid | 4.2378 | 4.4312 | 4.3407 | 2.04% | +2.43% |

The retained `[[gnu::always_inline]] inline` helper is in commit `db5edc48047c0725b4c9466676c233a1161580be`. It changes no prediction semantics or cache layout; the measured text segment grew by 16 KiB (1.3%). Eight selected regressions passed.

`inline.json` records timings and library hashes; `inline_compare.py` contains the benchmark procedure. The initial intermediate candidate is not archived. [Query sorting](QUERY_SORT_OPTIMIZATION.md) was optimized separately; deferring provisional cache writes remains unimplemented because it depends on each scheduler's reservation decisions.
