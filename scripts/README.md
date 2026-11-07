# Scripts

This directory contains reference schedulers, fixture generators, and trace
analysis utilities. Test commands and validation methodology are maintained in
the [Test Suite README](../tests/README.md) and
[Testing Guide](../docs/TESTING_GUIDE.md).

## Reference schedulers

- `python_reference_scheduler.py` — EASY-backfilling reference used to
  generate and compare expected schedules.
- `python_hybrid_scheduler.py` — hybrid/conservative-backfilling reference.
- `python_periodic_scheduler.py` — fixed periodic FCFS reference with optional
  arrival passes, plus tick-only EASY with strict reservation admission.
  Regression runners import its scheduling functions; its standalone CLI reads
  simple epoch CSV and writes a schedule CSV to stdout or `--outfile`.

```bash
python3 scripts/python_periodic_scheduler.py jobs.csv --nodes 4 \
  --dispatch-interval 5 --dispatch-queue-depth 2 --outfile reference.csv
```

Use `--dispatch-on-events` for FCFS arrival passes. `--backfill-policy easy`
uses strict completion-before-start admission and currently requires timer-only
dispatch. The reference has constant capacity and no cleanup delay, warm start,
planning horizon/resolution, or general hybrid/conservative protection.

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

- `detect_capacity_periods.py` — find sustained low-allocation/backlog or
  no-start/backlog periods and optionally emit a candidate capacity schedule.
  For more comprehensive analysis, use the `detect_queue_pause/` toolkit,
  which reconstructs binned demand and allocation, supports optional EASY-
  backfill evidence, and reports richer operating-state evidence. Both tools
  are heuristic; validate their candidates against scheduler and maintenance
  records.
- `prepare_warm_start_trace.py` — legacy workaround that materializes jobs
  crossing `t` as ordinary simulation records with their remaining durations.
  Prefer the replay-based `--sim_start_time` method, which preserves historical
  departures independently of `--run_time_mode` and suppresses historical seeds
  from output and statistics.
- `analyze_trace_performance.py` — summary performance analysis.
- `calculate_resource_trace.py` — derive resource occupancy from a schedule.
- `stitch_checkpoint_output.py` — reconstruct file-backed output from archived
  segments, or rebuild archived Redis CSV values, hashes, sets, and indexes.
- `trace/detect_abnormality.awk` — flag anomalous trace records.
- `trace/plot/resource_time.py` — plot resource use over time.
- `trace/plot/t_exec_limit.py` and `t_exec_limit2.py` — plot execution time
  against time limits.

The separate Fugaku experiment tools are documented in
[`experimental/fugaku-power/scripts/README.md`](../experimental/fugaku-power/scripts/README.md).

The maintenance tools and their assumptions are documented in
[Maintenance, Capacity Changes, and Warm Starts](../docs/user-guide/maintenance-and-warm-start.md).
