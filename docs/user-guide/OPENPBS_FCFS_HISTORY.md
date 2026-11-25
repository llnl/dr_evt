# OpenPBS Default-FCFS History

This historical comparison used DR_EVT's ordinary `fcfs` forecast instead of
`fcfs_1s_evt_group`. It is retained to explain why a separate OpenPBS-compatible
policy exists; it is not the current validation configuration.

## Result

| DR_EVT priority policy | Differing schedules out of 2,000 |
|---|---:|
| `fcfs_1s_evt_group` | 0 |
| `fcfs` | 1,433 |

The first large-workload difference occurs because OpenPBS groups forecast
releases at 13,006 and 13,007 seconds before checking a waiting job. Ordinary
DR_EVT `fcfs` checks after the release at 13,006. The different prediction
changes which later job may backfill.

:::{figure} ../_static/scheduler-validation/openpbs/default-fcfs/comparison.svg
:alt: Six-panel historical comparison of native OpenPBS and ordinary DR_EVT FCFS showing different job schedules
:width: 100%

Historical ordinary-`fcfs` comparison: aggregate metrics are similar, but
1,433 job schedules differ.
:::

| Predicted releases | OpenPBS first check | Ordinary DR_EVT first check |
|---|---:|---:|
| 10, 11, 100 | 11 | 10 |
| 10, 12, 100 | 10 | 10 |

`opt_backfill_fuzzy=off` does not remove this behavior: OpenPBS replaces its
zero value with one second when forming the inclusive forecast group.

## Reproduce

After preparing the pinned OpenPBS source and Lassen input as described on the
[current validation page](OPENPBS_VALIDATION.md), run:

```sh
OPENPBS_VALIDATION_RESULTS="$PWD/validation/openpbs/results/fcfs-history" \
  bash validation/openpbs/run.sh --priority_policy fcfs \
  --trace /dr_evt_source/validation/openpbs/results/jobs-relative.csv
```

The native source locations for this rule are `simulate_events()` and
`calc_run_time()` in OpenPBS `src/scheduler/simulate.cpp`. Recorded evidence is
also retained under `validation/openpbs/evidence/whole-seconds-20261108/`.
