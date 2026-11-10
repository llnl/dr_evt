# Start-prediction comparison

- **Cached lookup:** reuses live backfill estimates; unexamined jobs remain unknown. Estimates can be stale or unprotected.
- **Fresh-only forecast:** places full time-limit windows in a temporary timeline, without replaying future scheduling passes.
- **Cache-first:** reuses cached starts and forecasts gaps; refreshes starts before the current time.
- **Side simulation:** replays scheduling and backfill using estimated unfinished durations and only submitted jobs.

All methods query the full pending queue. Cost is summed mean query time divided by finite returned predictions. **Common MAE** compares the same job/checkpoint observations across all four methods; **Own MAE** uses each method's finite predictions. Errors compare estimates with an independent reference simulation.

## Actual execution durations

Event hybrid, R=32/Q=64, 2,000 jobs; median runtime is 29% of the time limit. There are 573 common observations and 9,348 per full-coverage method.

| Method | Coverage | µs / prediction | Common MAE (min) | Own MAE (min) |
| --- | ---: | ---: | ---: | ---: |
| Cached lookup | 6.13% | 0.138 | 106.73 | 106.73 |
| Fresh-only forecast | 100% | 0.258 | 155.88 | 547.40 |
| Cache-first extension | 100% | 0.271 | 112.36 | 544.74 |
| Side simulation, time-limit estimates | 100% | 8.754 | 155.88 | 647.25 |

## Execution duration equals time limit

There are 202 common observations and 12,768 per full-coverage method.

| Method | Coverage | µs / prediction | Common MAE (min) | Own MAE (min) |
| --- | ---: | ---: | ---: | ---: |
| Cached lookup | 1.58% | 0.420 | 49.34 | 49.34 |
| Fresh-only forecast | 100% | 0.236 | 0.28 | 112.39 |
| Cache-first extension | 100% | 0.254 | 44.04 | 113.08 |
| Side simulation, time-limit estimates | 100% | 6.237 | 0 | 0.003 |

## Interpretation

Duration uncertainty can outweigh scheduler fidelity. With actual runtimes, side simulation costs about 32× more per prediction than cache-first and has higher own-set error. With accurate durations, its own-set error falls to 0.003 minutes. Unknown future submissions explain the small remaining error.

Cache-first is a reasonable inexpensive default when durations are uncertain. Use side simulation for reliable estimates or policy-sensitive studies. These results establish neither a worst-case bound nor a duration-accuracy threshold for choosing replay.

`get_job_statuses(ids)` selects cache-first; `get_job_statuses(ids, 0, false)` selects fresh-only. `get_estimated_job_starts()` returns cached estimates. The benchmark sanitizes input before `get_exact_job_start_times()`; side-query costs include that preparation and its additional clone. Actual runtimes are used only for observed history and reference execution.

See [all ten configurations](cache-first/NORMALIZED_RESULTS.md) and [reproduction instructions](README.md).
