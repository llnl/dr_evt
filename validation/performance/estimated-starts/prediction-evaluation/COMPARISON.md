# Start-prediction comparison

- **Cached lookup:** reuses live backfill estimates; unexamined jobs remain unknown. Estimates can be stale or unprotected.
- **Forecast without backfill:** places full time-limit windows in a temporary timeline, without replaying future scheduling passes. Existing protected reservations still constrain the forecast.
- **Cache-first:** reuses cached starts and forecasts gaps; refreshes starts before the current time.
- **Side simulation:** replays scheduling and backfill using estimated unfinished durations and only submitted jobs.

All methods query the full pending queue. Cost is summed mean query time divided by finite returned predictions. Mean Absolute Error (MAE) averages absolute differences between estimated and reference starts. **Common MAE** compares the same job/checkpoint observations across all four methods; **Own MAE** uses each method's finite predictions. Errors compare estimates with an independent reference simulation.

## Actual execution durations

Event hybrid, R=32/Q=64, 2,000 jobs; median runtime is 29% of the time limit. There are 573 common observations and 9,348 per full-coverage method.

| Method | Coverage | µs / prediction | Common MAE (min) | Own MAE (min) |
| --- | ---: | ---: | ---: | ---: |
| Cached lookup | 6.13% | 0.138 | 106.73 | 106.73 |
| Forecast without backfill | 100% | 0.258 | 155.88 | 547.40 |
| Cache-first extension | 100% | 0.271 | 112.36 | 544.74 |
| Side simulation, time-limit estimates | 100% | 8.754 | 155.88 | 647.25 |

## Execution duration equals time limit

There are 202 common observations and 12,768 per full-coverage method.

| Method | Coverage | µs / prediction | Common MAE (min) | Own MAE (min) |
| --- | ---: | ---: | ---: | ---: |
| Cached lookup | 1.58% | 0.420 | 49.34 | 49.34 |
| Forecast without backfill | 100% | 0.236 | 0.28 | 112.39 |
| Cache-first extension | 100% | 0.254 | 44.04 | 113.08 |
| Side simulation, time-limit estimates | 100% | 6.237 | 0 | 0.003 |

## Interpretation

The **0.28 versus 49.34 minutes** comparison uses the same 202 job/checkpoint observations. Of the cached predictions, 96 protected reservations have zero error; 106 unprotected estimates have 94.02 minutes MAE, producing the combined 49.34 minutes. Unprotected cached estimates record when enough nodes become available, without placing every earlier waiting job's full execution window. They can therefore predict starts too early. That behavior is consistent with Slurm’s `squeue --start`. The forecast without backfill places those windows in queue order; with durations equal to time limits, this closely matches execution for this common set. Cached lookup reuses planning results, but does not provide a complete forecast of future backfill decisions.

Cache-first reuses accurate protected reservations **and** unprotected cached estimates. In this control, the latter account for its 44.04-minute common-set MAE, versus 0.28 minutes when cache reuse is disabled. It refreshes cached starts before the query time, reducing error from the cache-only result of 49.34 minutes, but retains other optimistic estimates. Accurate reservations therefore do not make the entire cache accurate. The protected-only cache option avoids reusing those unprotected estimates; these tables measure the original default cache-first mode, not that option.

The forecast without backfill assumes jobs occupy resources for their full time limits. When jobs finish earlier, it holds resources too long in its temporary timeline, and this error accumulates as it places later jobs. When execution duration equals the time limit, that duration assumption matches the reference run: own-set MAE falls from 547.40 to 112.39 minutes in these experiments. Residual error remains because the forecast does not replay future backfill admissions and dispatch rules. The two conditions have different observation sets; this is not a paired comparison of identical job/checkpoint observations.

Duration uncertainty can outweigh scheduler fidelity. With actual runtimes, side simulation costs about 32× more per prediction than cache-first and has higher own-set error. With accurate durations, its own-set error falls to 0.003 minutes. Unknown future submissions explain the small remaining error.

Cache-first is a reasonable inexpensive default when durations are uncertain. Use side simulation for reliable estimates or policy-sensitive studies. These results establish neither a worst-case bound nor a duration-accuracy threshold for choosing replay.

Disabling cache reuse recomputes reserved starts too. Recomputing can change those values because it builds a new plan from current state. However, a cached reservation often reflects the earliest fit after running jobs finish and earlier protected jobs are accounted for. The forecast can find the same fit without simulating future backfill, so omitting backfill does not always produce a noticeable difference.

`get_job_statuses(ids, 0, true, false)` reuses compatible protected reservation windows and forecasts unprotected jobs. `get_job_statuses(ids)` selects cache-first; `get_job_statuses(ids, 0, false)` selects the forecast without backfill. `get_start_times_by_est_caches()` returns cached estimates. The benchmark sanitizes input before `get_start_times_by_lookahead()`; side-query costs include that preparation and its additional clone. Actual runtimes are used only for observed history and reference execution.

See [all ten configurations](cache-first/NORMALIZED_RESULTS.md) and [reproduction instructions](README.md).
