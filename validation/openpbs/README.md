# Native OpenPBS validation

This harness compares DR_EVT with the unmodified OpenPBS 23.06.06 scheduler
library. It emulates server snapshots, launch acknowledgements, completions, and
virtual time; it does not run `pbs_server`, a database, or MOM daemons.

## Results

| Case | Compared result | Differences |
|---|---|---:|
| Capacity, `fcfs_1s_evt_group` | Start/completion times | 0 of 2,000 |
| Placement, `fcfs_1s_evt_group` | Start/completion times | 0 of 2,000 |
| Placement, `fcfs_1s_evt_group` | Exact vnode lists | 0 of 2,000 |

Capacity mode gives each job a private vnode and requests its original node
count from a shared 756-unit `capacity` resource. Placement mode uses 756 shared
exclusive vnodes. Both use strict FCFS order, R=32, unlimited candidate
examination, and no node-release delay.

Result interpretation and plots are on the
[validation page](../../docs/user-guide/OPENPBS_VALIDATION.md).

## Options

| Setting | Purpose |
|---|---|
| `--priority_policy` | Select the OpenPBS-compatible `fcfs_1s_evt_group` policy. |
| `--resource-model` | The runner executes both `capacity` and `placement`. |
| `OPENPBS_VALIDATION_MAX_JOBS` | Select the Lassen prefix size; default 2,000, and 0 means all jobs. |

The adapter calls native `scheduling_cycle()` after each equal-time arrival and
completion batch. Native query conversion, ordering, eligibility, vnode
selection, future-start search, and calendar updates remain in the OpenPBS
library. Generated results record source hashes, exact commands, configuration,
converted inputs, event records, schedules, and audits.

## Reproduce

From the repository root:

```sh
mkdir -p external
git clone https://github.com/openpbs/openpbs.git external/openpbs
git -C external/openpbs checkout --detach cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a

mkdir -p validation/openpbs/results
python3 -c 'import csv; r=list(csv.DictReader(open("validation/fixtures/jobs.csv"))); o=int(r[0]["submit_time"]); w=csv.DictWriter(open("validation/openpbs/results/jobs-relative.csv","w",newline=""),fieldnames=r[0].keys()); w.writeheader(); [w.writerow({**x,"submit_time":int(x["submit_time"])-o}) for x in r]'

OPENPBS_VALIDATION_RESULTS="$PWD/validation/openpbs/results/reproduction" \
  bash validation/openpbs/run.sh --priority_policy fcfs_1s_evt_group \
  --trace /dr_evt_source/validation/openpbs/results/jobs-relative.csv
```

Native OpenPBS has been validated only with submission times that start at
zero. The checked-in `validation/fixtures/jobs.csv` uses absolute Unix
timestamps; given that file directly, native OpenPBS produced 1,943 capacity
start times that differ from the published schedule, while DR_EVT matched the
published schedule.
The commands above therefore write the same 2,000 jobs with the first
submission at time 0 and pass that file with `--trace`, which overrides the
runner's default fixture path. Expected schedules in `validation/fixtures/`
remain in absolute time: add `1597836579` to compare them with these results.

One invocation writes separate `capacity/` and `placement/` result trees. Use a
fresh output directory for each campaign. The default reusable build volume is
`dr-evt-openpbs-build`; override it with `OPENPBS_VALIDATION_BUILD_VOLUME` after
changing native revisions or configure options. Compact expected schedules are
in `validation/fixtures/`.

Run a short smoke campaign:

```sh
OPENPBS_VALIDATION_MAX_JOBS=50 bash validation/openpbs/run.sh \
  --priority_policy fcfs_1s_evt_group \
  --trace /dr_evt_source/validation/openpbs/results/jobs-relative.csv
```

Run harness audit tests without native OpenPBS:

```sh
python3 -m unittest discover -s validation/openpbs -p test_compare.py -v
```

Plot a completed report:

```sh
python3 validation/openpbs/plot.py path/to/report.json
```

The historical ordinary-`fcfs` comparison and evidence are indexed by the
[history page](../../docs/user-guide/OPENPBS_FCFS_HISTORY.md).
