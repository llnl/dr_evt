# Native Slurm validation

This harness compares DR_EVT with native Slurm using [virtual time](VIRTUAL_TIME.md) and native
allocation, completion, selection, and backfill paths. It runs two distinct
resource models on the 2,000-job Lassen workload.

## Results

| Case | Native resources | Compared result | Differences |
|---|---|---|---:|
| Capacity | Private node per job and 756 shared `capacity` licenses | Start/completion times | 2 of 2,000 |
| Placement | 756 shared exclusive nodes | Start times | 0 of 2,000 |
| Placement | 756 shared exclusive nodes | Exact node lists | 0 of 2,000 |

Result interpretation and plots are on the
[validation page](../../docs/user-guide/SLURM_VALIDATION.md).

## Options

| Setting | Purpose |
|---|---|
| `--resource-model` | Select `capacity` (private nodes plus licenses) or `placement` (shared nodes). |
| `--bf-licenses on` | Enable future license planning in capacity mode; placement ignores it. |
| `--batch-main-dispatch` | Batch equal-time RPCs before releasing scheduling. |
| `bf_interval=30` | Run native periodic backfill every 30 virtual seconds. |
| `bf_job_part_count_reserve=32` | Protect 32 waiting-job plans. |
| `bf_max_job_part=64` | Examine 64 jobs per partition in a backfill pass. |
| `bf_window=4320` | Plan 4,320 minutes (72 hours) ahead. |

The adapter registers virtual nodes and sends native job allocation, completion,
and epilog RPCs. No compute daemon or job process runs. Native Slurm retains
ownership of queue, reservation, and node-selection decisions. Source revisions,
configuration, hooks, and binary hashes are saved with each run.

## Reproduce

From the repository root:

```sh
mkdir -p external
git clone https://github.com/SchedMD/slurm.git external/slurm
git -C external/slurm checkout --detach 17abb848e20054009d136a2c504a23e5462f26c8

SLURM_LICENSE_RESULTS="$PWD/validation/slurm/results/reproduction" \
  bash validation/slurm/run.sh
```

The runner uses the checked-in `validation/fixtures/jobs.csv`. Pass another
path with `event_trace.py --trace` only when validating a different workload.

The runner builds `dr-evt-slurm-validation`, executes small controls, and writes
these result trees:

| Directory | Contents |
|---|---|
| `controls/` | Small event, reservation, and endpoint controls |
| `paired/` | Capacity comparison with `bf_licenses` enabled |
| `paired-license-off/` | Historical no-license-reservation control |
| `placement/` | Default-Slurm exact-placement comparison |

Use a fresh output directory for each campaign. The default reusable build
volume is `dr-evt-slurm-license-build`; override it with
`SLURM_LICENSE_BUILD_VOLUME` after changing the Slurm revision or configure
options. Compact expected schedules are in `validation/fixtures/`.

Run the harness tests without native Slurm:

```sh
python3 -m unittest validation/slurm/test_audit.py \
  validation/slurm/test_event_trace.py -v
```

Audit or plot a completed case:

```sh
python3 validation/slurm/licenses/audit_event.py path/to/case
python3 path/to/case/plot.py path/to/case/plot-input.json
```

Historical EPFL tools remain in `EPFL_REFERENCE.md` and `run_epfl.sh`.
License-off, startup, and scheduler-order evidence is indexed by the
[history page](../../docs/user-guide/SLURM_LICENSE_OFF_DIAGNOSTICS.md).
