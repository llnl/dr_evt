# Scripts

This directory contains reference schedulers, fixture generators, and trace
analysis utilities. Test commands and validation methodology are maintained in
the [Test Suite README](../tests/README.md) and
[Testing Guide](../docs/TESTING_GUIDE.md).

## Reference schedulers

- `python_reference_scheduler.py` — EASY-backfilling reference used to
  generate and compare expected schedules.
- `python_conservative_scheduler.py` — conservative-backfilling reference.

Their algorithmic context is documented in
[Backfilling Algorithms](../docs/BACKFILLING_ALGORITHMS.md).

## Fixture generators

- `generators/generate_all_expected_outputs.py` — scheduler-correctness
  expected outputs.
- `generators/generate_scale_expected_outputs.py` — scale-test expected
  outputs.
- `generators/generate_large_test.py` — synthetic large traces.

The owning fixtures and runner commands are listed in
[`tests/README.md`](../tests/README.md).

## Trace analysis

- `analyze_trace_performance.py` — summary performance analysis.
- `calculate_resource_trace.py` — derive resource occupancy from a schedule.
- `trace/detect_abnormality.awk` — flag anomalous trace records.
- `trace/plot/resource_time.py` — plot resource use over time.
- `trace/plot/t_exec_limit.py` and `t_exec_limit2.py` — plot execution time
  against time limits.

The separate Fugaku experiment tools are documented in
[`experimental/fugaku-power/scripts/README.md`](../experimental/fugaku-power/scripts/README.md).
