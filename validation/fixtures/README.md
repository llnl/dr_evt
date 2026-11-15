# Native validation fixtures

These compact CSVs record the 2,000-job Lassen workload and expected
native schedules used by the current Slurm and OpenPBS validation pages.

| File | Contents |
|---|---|
| `jobs.csv` | Absolute Unix submit time, node count, requested time limit, and actual runtime |
| `slurm-capacity.csv` | Native Slurm and DR_EVT times for the `bf_licenses` capacity comparison |
| `slurm-placement.csv` | Native default-Slurm times and exact node IDs |
| `openpbs-capacity.csv` | Native OpenPBS `fcfs_1s_evt_group` times |
| `openpbs-placement.csv` | Native OpenPBS `fcfs_1s_evt_group` times and exact vnode IDs |

All files use LF line endings and absolute Unix timestamps. Job IDs are
one-based; node IDs are zero-based and semicolon-separated. The first submission
is `1597836579`.

## Expected differences

| Fixture | Start differences | Node differences |
|---|---:|---:|
| Slurm capacity | 2 | not compared |
| Slurm placement | 0 | 0 |
| OpenPBS capacity | 0 | not compared |
| OpenPBS placement | 0 | 0 |

`export.py` regenerates these files from audited result reports. Run it from the
repository root and supply the four report paths:

```sh
python3 validation/fixtures/export.py \
  --slurm-capacity-report path/to/slurm-capacity/report.json \
  --slurm-placement-report path/to/slurm-placement/report.json \
  --openpbs-capacity-report path/to/openpbs-capacity/report.json \
  --openpbs-placement-report path/to/openpbs-placement/report.json
```

The original LAST extraction used to create `jobs.csv` is not included. Its SHA-256 is
`463379d4f61ba1fb75e2ec32380538bb3e44f00b9210ff67db3327aa8715dd04`.

Verify fixture completeness and expected differences without native schedulers:

```sh
python3 tests/test_validation_fixtures.py -v
# or after configuring unit tests:
ctest --test-dir build -R test_validation_fixtures --output-on-failure
```
