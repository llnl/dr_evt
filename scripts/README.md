# Scripts

This directory contains reference schedulers, fixture generators, and trace
analysis utilities. Test commands and validation methodology are maintained in
the [Test Suite README](../tests/README.md) and
[Testing Guide](../docs/TESTING_GUIDE.md).

## Reference schedulers

Each reference is an independent Python model of one scheduling loop. Use the
row that matches the scheduler behavior being checked:

| Script | Loop and resource model | Production behavior represented |
|---|---|---|
| `python_reference_scheduler.py` | Event-driven EASY backfill; capacity counts | General EASY reference for expected schedules |
| `python_hybrid_scheduler.py` | Event-driven or periodic hybrid/conservative backfill; capacity counts | Slurm with `bf_licenses` (quantity-based license pool), using `--backfill-interval` |
| `python_periodic_scheduler.py` | Fixed periodic dispatch: one pass per tick, optional arrival passes; capacity counts | DR_EVT's LSF model (EASY, strict admission) and the FCFS part of Spur's timer-plus-submission loop |
| `python_periodic_backfill_placement_scheduler.py` | Event-driven FCFS dispatch plus periodic backfill; exact whole-node placement | Default Slurm (`bf_running_job_reserve` off) on exclusive, equal-weight, flat-topology nodes |

| Use case | Script | Options |
|---|---|---|
| Event-driven EASY, capacity | `python_reference_scheduler.py` | `--nodes N` |
| Slurm `bf_licenses`: periodic hybrid backfill, capacity | `python_hybrid_scheduler.py` | `--backfill-interval 30 --reservation-depth 32 --backfill-queue-depth 64 --planning-time-resolution 1 --planning-horizon 259200` |
| LSF model: EASY with strict admission | `python_periodic_scheduler.py` | `--dispatch-interval S --backfill-policy easy`; compare with DR_EVT built with `-DDR_EVT_LEGACY_RESERVATION_BARRIER=ON` |
| Fixed-tick or Spur-style FCFS | `python_periodic_scheduler.py` | `--dispatch-interval S [--dispatch-on-events] [--dispatch-queue-depth Q]` |
| Default Slurm, exact placement | `python_periodic_backfill_placement_scheduler.py` | `--nodes 756 --backfill-interval 30 --reservation-depth 32 --backfill-queue-depth 64`, with absolute timestamps |

Notes on scope:

- `python_periodic_scheduler.py` models DR_EVT's LSF configuration, which is
  derived from IBM documentation; no LSF source or native LSF run was
  compared. Its arrival-pass mode covers Spur's wake pattern but not Spur's
  conservative backfill. Validated OpenPBS behavior is event-driven with
  one-second forecast grouping and is not covered by this script.
- `python_periodic_backfill_placement_scheduler.py` uses timestamps as given.
  Its release-grouping windows align to the input clock, so supply the
  absolute timestamps the compared scheduler sees. It writes
  `<trace>_periodic_backfill_placement_reference.csv`, including selected
  node IDs, to `/tmp` by default or to `--outdir`.
- `python_periodic_scheduler.py` regression runners import its scheduling
  functions; its standalone CLI reads simple epoch CSV and writes a schedule
  CSV to stdout or `--outfile`.

```bash
python3 scripts/python_periodic_scheduler.py jobs.csv --nodes 4 \
  --dispatch-interval 5 --dispatch-queue-depth 2 --outfile reference.csv
```

Use `--dispatch-on-events` for FCFS arrival passes. `--backfill-policy easy`
uses strict completion-before-start admission and currently requires timer-only
dispatch. The periodic reference has constant capacity and no cleanup delay,
warm start, planning horizon/resolution, or general hybrid/conservative
protection.

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
