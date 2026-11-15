# Slurm Validation

DR_EVT is compared with native Slurm 26.11.0-0rc2 at commit
`17abb848e20054009d136a2c504a23e5462f26c8` on the shared
[2,000-job Lassen workload](SCHEDULER_VALIDATION.md#lassen-workload). Both cases
use R=32, Q=64, a 30-second backfill interval, and a 72-hour planning window.

## Results

| Resource model | Native Slurm setup | Compared property | Result |
|---|---|---|---:|
| Capacity | Private node per job plus 756 shared `capacity` licenses; `bf_licenses` enabled | Start and completion times | 2 differing jobs out of 2,000 |
| Placement | 756 shared exclusive nodes; default Slurm without `bf_licenses` or `bf_running_job_reserve` | Start times | 0 differing jobs |
| Placement | Same as above | Exact node allocations | 0 differing jobs |

The two capacity differences are jobs 560 and 614 in one-based reporting (559
and 613 in zero-based records). Native main dispatch and backfill can run in a
different order when both are released; the adapter batches events but does not
force their order. DR_EVT's capacity schedule is unchanged from the previously
published exact-match run.

:::{figure} ../_static/scheduler-validation/slurm/capacity-current/comparison.svg
:alt: Six-panel comparison of native Slurm and DR_EVT capacity schedules for the 2,000-job Lassen workload
:width: 100%

Capacity comparison with `bf_licenses`: two job starts differ because native
main dispatch and backfill order is uncontrolled.
:::

The placement comparison covers one homogeneous partition, equal node weights,
exclusive one-CPU nodes, `select/cons_tres`, and flat topology. It does not claim
agreement for heterogeneous resources, GRES, features, fairshare, or preemption.

:::{figure} ../_static/scheduler-validation/slurm/placement-current/comparison.svg
:alt: Six-panel comparison of native default Slurm and DR_EVT exact-placement schedules for the 2,000-job Lassen workload
:width: 100%

Default-Slurm placement comparison: all start times and exact node allocations
match.
:::

## Options

| Option or setting | Purpose |
|---|---|
| `--resource-model capacity` | Compare temporal scheduling with interchangeable capacity; native jobs use private nodes and shared licenses. |
| `--resource-model placement` | Compare both timing and exact node identities on one shared node pool. |
| `bf_licenses` | Protect future shared-license capacity in the capacity case. It is not used in placement mode because enabling it also changes running-job reservation behavior. |
| `--batch-main-dispatch` | Submit each equal-time event batch before releasing native scheduling. It does not order main dispatch relative to backfill. |
| `--backfill_interval 30` | Use event-driven FCFS dispatch plus a 30-second periodic backfill pass in DR_EVT. |
| `--reservation_depth 32` | Protect up to 32 waiting-job plans per backfill pass. |
| `--backfill_queue_depth 64` | Examine at most 64 eligible waiting jobs per pass. |

Placement planning uses a fresh waiting-job reservation profile each cycle.
Running jobs are released by requested limit in clock-aligned groups, and node
selection uses contiguous-run best fit. Backfill checks the requested endpoint;
actual allocations remain half-open.

## Reproduce

Follow the source and workload setup in `validation/slurm/README.md`, then run
the complete campaign from the repository root:

```sh
SLURM_LICENSE_RESULTS="$PWD/validation/slurm/results/reproduction" \
  bash validation/slurm/run.sh
```

The harness README describes outputs, audits, and build-volume controls. Compact
expected schedules are checked in under `validation/fixtures/`.

Run only the placement comparison inside the prepared validation container:

```sh
SLURM_LICENSE_STATE_ONLY=1 \
python3 validation/slurm/licenses/event_trace.py \
  --batch-main-dispatch --resource-model placement \
  --trace validation/fixtures/jobs.csv \
  --outdir /results/placement
```

The harness details are in
[`validation/slurm/README.md`](https://github.com/LLNL/dr_evt/tree/main/validation/slurm).
Earlier license-on/off, endpoint, skipped-tick, and scheduler-order experiments
are retained in [Slurm Historical Diagnostics](SLURM_LICENSE_OFF_DIAGNOSTICS.md).

```{toctree}
:maxdepth: 1
:hidden:

SLURM_LICENSE_OFF_DIAGNOSTICS
```
