# Query cost per predicted job and start-time errors

All methods query the full eligible pending queue.

**Cost** is summed mean query time divided by finite returned predictions, including query costs at zero-prediction checkpoints. **Common MAE** uses the intersection of finite predictions across methods, matched by job and checkpoint; **Own MAE** uses each method's finite predictions. Errors compare estimates with independent reference execution. A job contributes once per checkpoint. Undefined metrics are shown as —.

**Side simulation** uses only submitted jobs and time-limit estimates for unfinished durations and completion events. Timing includes input preparation and replay. Actual durations establish observed history and reference execution; the control executes for time limits. `evaluate_observable.cpp` enforces this restriction before calling the exact API.

**Cached** = `get_estimated_job_starts()`; **Fresh** = `get_job_statuses(ids, 0, false)`; **Side simulation** = `get_exact_job_start_times(ids)`.

**Cache-first** = `get_job_statuses(ids)`: reuse cached starts and forecast missing starts.

## Actual execution durations

| Configuration | API | Coverage | µs / predicted job | Common-set MAE (min) | Own-set MAE (min) |
| --- | --- | ---: | ---: | ---: | ---: |
| Flux FCFS / no backfill | Cached | 0.00% | — | — | — |
| Flux FCFS / no backfill | Fresh | 100.00% | 0.259 | — | 506.23 |
| Flux FCFS / no backfill | Cache-first | 100.00% | 0.254 | — | 506.23 |
| Flux FCFS / no backfill | Side simulation | 100.00% | 4.960 | — | 643.68 |
| Flux-style pass accounting | Cached | 4.52% | 0.169 | 179.53 | 179.53 |
| Flux-style pass accounting | Fresh | 100.00% | 0.258 | 179.53 | 551.29 |
| Flux-style pass accounting | Cache-first | 100.00% | 0.272 | 179.53 | 551.29 |
| Flux-style pass accounting | Side simulation | 100.00% | 15.471 | 179.53 | 658.51 |
| Slurm-style periodic protection | Cached | 7.39% | 0.119 | 116.14 | 116.14 |
| Slurm-style periodic protection | Fresh | 100.00% | 0.253 | 189.90 | 546.24 |
| Slurm-style periodic protection | Cache-first | 100.00% | 0.268 | 171.80 | 545.65 |
| Slurm-style periodic protection | Side simulation | 100.00% | 9.622 | 191.78 | 645.25 |
| Slurm license-off diagnostic | Cached | 0.00% | — | — | — |
| Slurm license-off diagnostic | Fresh | 100.00% | 0.286 | — | 760.61 |
| Slurm license-off diagnostic | Cache-first | 100.00% | 0.283 | — | 760.61 |
| Slurm license-off diagnostic | Side simulation | 100.00% | 3.812 | — | 247.59 |
| OpenPBS, ordinary forecast | Cached | 6.04% | 0.138 | 165.87 | 165.87 |
| OpenPBS, ordinary forecast | Fresh | 100.00% | 0.239 | 165.87 | 672.76 |
| OpenPBS, ordinary forecast | Cache-first | 100.00% | 0.248 | 165.87 | 672.76 |
| OpenPBS, ordinary forecast | Side simulation | 100.00% | 12.731 | 165.87 | 629.25 |
| OpenPBS, 1s event grouping | Cached | 5.93% | 0.138 | 142.40 | 142.40 |
| OpenPBS, 1s event grouping | Fresh | 100.00% | 2.944 | 142.40 | 677.38 |
| OpenPBS, 1s event grouping | Cache-first | 100.00% | 2.900 | 142.40 | 677.38 |
| OpenPBS, 1s event grouping | Side simulation | 100.00% | 28.914 | 142.40 | 630.90 |
| EASY | Cached | 0.15% | 4.445 | 40.58 | 40.58 |
| EASY | Fresh | 100.00% | 0.260 | 40.58 | 607.75 |
| EASY | Cache-first | 100.00% | 0.278 | 40.58 | 607.75 |
| EASY | Side simulation | 100.00% | 5.819 | 40.58 | 709.62 |
| Event hybrid | Cached | 6.13% | 0.138 | 106.73 | 106.73 |
| Event hybrid | Fresh | 100.00% | 0.258 | 155.88 | 547.40 |
| Event hybrid | Cache-first | 100.00% | 0.271 | 112.36 | 544.74 |
| Event hybrid | Side simulation | 100.00% | 8.754 | 155.88 | 647.25 |
| Event conservative | Cached | 6.10% | 0.132 | 196.90 | 196.90 |
| Event conservative | Fresh | 100.00% | 0.256 | 196.90 | 543.36 |
| Event conservative | Cache-first | 100.00% | 0.270 | 196.90 | 543.36 |
| Event conservative | Side simulation | 100.00% | 9.675 | 196.90 | 646.75 |
| Fixed dispatch, 30s | Cached | 6.41% | 0.133 | 133.50 | 133.50 |
| Fixed dispatch, 30s | Fresh | 100.00% | 0.230 | 249.25 | 556.21 |
| Fixed dispatch, 30s | Cache-first | 100.00% | 0.243 | 223.10 | 554.51 |
| Fixed dispatch, 30s | Side simulation | 100.00% | 20.794 | 249.31 | 656.00 |

Event hybrid: 573 common job/checkpoint observations; 573 cached predictions and 9348 per full-coverage method.

## Execution duration equals time limit

| Configuration | API | Coverage | µs / predicted job | Common-set MAE (min) | Own-set MAE (min) |
| --- | --- | ---: | ---: | ---: | ---: |
| Flux FCFS / no backfill | Cached | 0.00% | — | — | — |
| Flux FCFS / no backfill | Fresh | 100.00% | 0.241 | — | 172.36 |
| Flux FCFS / no backfill | Cache-first | 100.00% | 0.237 | — | 172.36 |
| Flux FCFS / no backfill | Side simulation | 100.00% | 3.556 | — | 0.00 |
| Flux-style pass accounting | Cached | 3.26% | 0.217 | 0.00 | 0.00 |
| Flux-style pass accounting | Fresh | 100.00% | 0.236 | 0.00 | 112.91 |
| Flux-style pass accounting | Cache-first | 100.00% | 0.255 | 0.00 | 112.91 |
| Flux-style pass accounting | Side simulation | 100.00% | 11.886 | 0.00 | 0.00 |
| Slurm-style periodic protection | Cached | 1.52% | 0.434 | 57.72 | 57.72 |
| Slurm-style periodic protection | Fresh | 100.00% | 0.240 | 1.63 | 122.43 |
| Slurm-style periodic protection | Cache-first | 100.00% | 0.260 | 7.81 | 122.53 |
| Slurm-style periodic protection | Side simulation | 100.00% | 6.621 | 0.00 | 0.00 |
| Slurm license-off diagnostic | Cached | 0.00% | — | — | — |
| Slurm license-off diagnostic | Fresh | 100.00% | 0.284 | — | 742.98 |
| Slurm license-off diagnostic | Cache-first | 100.00% | 0.281 | — | 742.98 |
| Slurm license-off diagnostic | Side simulation | 100.00% | 2.774 | — | 18.22 |
| OpenPBS, ordinary forecast | Cached | 3.46% | 0.213 | 0.00 | 0.00 |
| OpenPBS, ordinary forecast | Fresh | 100.00% | 0.241 | 0.00 | 53.57 |
| OpenPBS, ordinary forecast | Cache-first | 100.00% | 0.255 | 0.00 | 53.57 |
| OpenPBS, ordinary forecast | Side simulation | 100.00% | 11.045 | 0.00 | 1.52 |
| OpenPBS, 1s event grouping | Cached | 3.46% | 0.213 | 0.00 | 0.00 |
| OpenPBS, 1s event grouping | Fresh | 100.00% | 3.186 | 0.00 | 58.57 |
| OpenPBS, 1s event grouping | Cache-first | 100.00% | 3.204 | 0.00 | 58.57 |
| OpenPBS, 1s event grouping | Side simulation | 100.00% | 20.519 | 0.00 | 3.91 |
| EASY | Cached | 0.10% | 6.124 | 0.00 | 0.00 |
| EASY | Fresh | 100.00% | 0.268 | 0.00 | 121.82 |
| EASY | Cache-first | 100.00% | 0.261 | 0.00 | 121.82 |
| EASY | Side simulation | 100.00% | 4.199 | 0.00 | 0.01 |
| Event hybrid | Cached | 1.58% | 0.420 | 49.34 | 49.34 |
| Event hybrid | Fresh | 100.00% | 0.236 | 0.28 | 112.39 |
| Event hybrid | Cache-first | 100.00% | 0.254 | 44.04 | 113.08 |
| Event hybrid | Side simulation | 100.00% | 6.237 | 0.00 | 0.00 |
| Event conservative | Cached | 2.55% | 0.271 | 0.00 | 0.00 |
| Event conservative | Fresh | 100.00% | 0.234 | 0.00 | 113.32 |
| Event conservative | Cache-first | 100.00% | 0.251 | 0.00 | 113.32 |
| Event conservative | Side simulation | 100.00% | 7.052 | 0.00 | 0.00 |
| Fixed dispatch, 30s | Cached | 1.03% | 0.644 | 45.31 | 45.31 |
| Fixed dispatch, 30s | Fresh | 100.00% | 0.181 | 0.31 | 114.05 |
| Fixed dispatch, 30s | Cache-first | 100.00% | 0.202 | 16.31 | 114.21 |
| Fixed dispatch, 30s | Side simulation | 100.00% | 15.629 | 0.00 | 0.00 |

Event hybrid: 202 common job/checkpoint observations; 202 cached predictions and 12768 per full-coverage method.

## Interpretation

Cache-first is an inexpensive default when durations are uncertain. Replay is useful with accurate estimates or for policy-sensitive studies. These results establish neither a worst-case bound nor an accuracy threshold for choosing replay.

## Reproduction

`summary.json` records metrics and prediction counts. Raw measurements are generated locally.

Build the benchmark following `validation/performance/estimated-starts/prediction-evaluation/README.md`, then regenerate the evaluation:

```bash
python3 validation/performance/estimated-starts/prediction-evaluation/run.py \
  /tmp/evaluate_predictions --repo "$PWD" --observable \
  --outdir /tmp/dr_evt_prediction_results \
  --workdir /tmp/dr_evt_prediction_runs
```
