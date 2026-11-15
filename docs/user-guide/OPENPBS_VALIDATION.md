# OpenPBS Validation

DR_EVT is compared with the unmodified OpenPBS 23.06.06 scheduler library at
commit `cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a`. Both cases use the integer-second
conversion of the shared [2,000-job Lassen workload](SCHEDULER_VALIDATION.md#lassen-workload),
strict FCFS order, R=32, unlimited candidate examination, and
`opt_backfill_fuzzy=off`.

## Results

| Resource model | Compared property | Result |
|---|---|---:|
| Capacity | Start and completion times | 0 differing jobs out of 2,000 |
| Placement | Start and completion times | 0 differing jobs out of 2,000 |
| Placement | Exact vnode allocations | 0 differing jobs out of 2,000 |

Capacity mode gives each job a private vnode and requests its original node
count from a shared 756-unit `capacity` resource. Placement mode instead uses
756 shared exclusive vnodes and compares their exact identities.

:::{figure} ../_static/scheduler-validation/openpbs/capacity-current/comparison.svg
:alt: Six-panel comparison of native OpenPBS and DR_EVT shared-capacity schedules for the 2,000-job Lassen workload
:width: 100%

Capacity comparison with `fcfs_1s_evt_group`: every start and completion
matches.
:::

:::{figure} ../_static/scheduler-validation/openpbs/placement-current/comparison.svg
:alt: Six-panel comparison of native OpenPBS and DR_EVT exact-placement schedules for the 2,000-job Lassen workload
:width: 100%

Placement comparison with `fcfs_1s_evt_group`: every start, completion, and
exact vnode allocation matches.
:::

The result requires DR_EVT's `fcfs_1s_evt_group` policy. OpenPBS groups
hypothetical forecast events from the earliest event T through T+1 second,
inclusive, before testing the waiting job. This affects future predictions, not
the actual arrival or completion timestamps.

## Options

| Option or setting | Purpose |
|---|---|
| `--priority_policy fcfs_1s_evt_group` | Reproduce OpenPBS's inclusive one-second forecast groups. |
| `--resource-model capacity` | Compare temporal scheduling with one shared consumable capacity pool. |
| `--resource-model placement` | Compare timing and exact vnode identities on a shared node pool. |
| `--backfill_policy hybrid` | Permit later jobs to run while preserving protected waiting-job starts. |
| `--reservation_depth 32` | Protect up to 32 waiting jobs. |
| `--per_pass_accounting` | Match the native event-driven scheduling-cycle accounting. |
| `opt_backfill_fuzzy=off` | Select the finest native forecast setting; OpenPBS still uses a one-second inclusive group. |

The validation covers one queue, FCFS ranks, homogeneous whole-node requests,
and no fairshare, preemption, dependencies, administrative reservations, or
node-release delay.

## Reproduce

Follow the source and workload setup in `validation/openpbs/README.md`, then run
capacity and placement validation together:

```sh
OPENPBS_VALIDATION_RESULTS="$PWD/validation/openpbs/results/reproduction" \
  bash validation/openpbs/run.sh --priority_policy fcfs_1s_evt_group
```

The harness README describes the short smoke run, outputs, audits, and
build-volume controls. Compact expected schedules are checked in under
`validation/fixtures/`. The harness details are in
[`validation/openpbs/README.md`](https://github.com/LLNL/dr_evt/tree/main/validation/openpbs).

The ordinary DR_EVT `fcfs` comparison and its 1,433 differing schedules are
retained in [OpenPBS FCFS History](OPENPBS_FCFS_HISTORY.md).

```{toctree}
:maxdepth: 1
:hidden:

OPENPBS_FCFS_HISTORY
```
