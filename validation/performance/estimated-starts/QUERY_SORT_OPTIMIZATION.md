# Sort only known cached starts

Cached queries partition known starts ahead of unknown jobs and stably sort only known values. Empty and all-unknown responses skip sorting; all-known responses skip partitioning. Equal-time starts and unknown jobs retain queue order.

The shared implementation applies to all schedulers and trace types. Predictions, coverage, timestamps, and live planning are unchanged. It adds no persistent state. Query cost is O(N + P + K log K): examined queue population N, cached records P, and known predictions K.

## Results

Native ARM64 Release builds; baseline `db5edc48047c0725b4c9466676c233a1161580be`. Medians of nine alternating runs after one warmup, timing only the consumed query response. Hybrid R=32, 100 nodes: a running job occupies 99 nodes until t=1000; waiting jobs need 100 nodes for 10 seconds. All-unknown uses backfill NONE.

| Eligible pending jobs | Prediction coverage | Original (µs/query) | Optimized (µs/query) | Reduction |
| ---: | --- | ---: | ---: | ---: |
| 1,000 | Q=64 | 10.85 | 9.04 | 16.6% |
| 10,000 | Q=64 | 115.61 | 72.93 | 36.9% |
| 10,000 | All unknown | 91.68 | 45.72 | 50.1% |
| 10,000 | All known | 317.08 | 310.37 | 2.1% |

Sparse queries improved about 37%; all-unknown queries improved about 50%. These are query-latency results, with no additional live-scheduling work.

Eight selected regressions passed. Ordering checks cover empty/all-unknown results, equal-time starts, and interleaved known/unknown entries on circular and deque queues.

`query-sort.json` records timings and hashes. `benchmark_query_sort.cpp` and `compare_query_sort.py` define the experiment; the comparison script requires local before/after builds at its configured paths.
