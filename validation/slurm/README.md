# Periodic backfill validation with native Slurm

The default comparison uses native Slurm from `external/slurm` with pooled
license capacity and our trace-driven validation adapter. Run it with:

```sh
bash validation/slurm/run.sh
```

It runs the small controls and the chronological 2,000-job Lassen prefix
with `bf_licenses` enabled and disabled. Lassen results are saved in `paired/`
and `paired-license-off/`, respectively.
Results, copied scripts, plotting inputs, resource curves, and PDF/SVG/PNG
figures are saved under `results/licenses/`. Set `SLURM_LICENSE_RESULTS` to
a fresh output directory to choose its location.
The result root also contains `license-planning-metrics.csv`,
`license-planning-comparison.json`, and `summarize_licenses.py` for the
matched on/off comparison.

With `select/cons_tres` and `bf_licenses`, 756 global `capacity` licenses
represent the `pbatch` node capacity. Each job requests its original node
count as licenses and has a private dummy compute node. License counts have
no individual node identities, so node binding does not constrain this pool.
Disabling `bf_licenses` removes future license reservations while retaining
the same license capacity limits and physical-node abstraction.
Backfill remains enabled at 30-second intervals; dispatch throttling is zero.
DR_EVT selects `FCFSPeriodicHybridCircularScheduler`, implemented in
`src/sim/scheduler_fcfs_periodic_hybrid_circular.cpp`, with FCFS ordering,
`--queue_impl circular --backfill_policy hybrid --dispatch_interval 0
--backfill_interval 30`. State events dispatch FCFS jobs; timer ticks backfill.
The adapter supplies virtual time and native registration, allocation,
completion, and epilog RPCs. A recorded synchronization hook serializes event
batches with native backfill; allocation and reservation algorithms remain
native Slurm. The lightweight observer fetches full job records on state
transitions; pending-job forecast timestamps may be stale in snapshots.

Small-case figures show feasible assignments to node-equivalent rows for
license planning on, off, and DR_EVT. The Lassen figure has six metric panels.
These experiments stay here until the comparison is ready for documentation.

Both schedulers use reservation depth 32, backfill examination depth 64,
planning resolution one second, and a 72-hour planning horizon. The Lassen
prefix starts with an empty `pbatch` capacity pool. Recorded runtimes are
rounded up to whole seconds in both inputs; arrivals and whole-minute time
limits are preserved. `workload.json` records the input hash and selected rows.

The adapter records native job states and license usage at event and timer
boundaries. `report.json` compares starts, waiting times, utilization, makespan,
and resource curves. `audit.json` independently verifies arrivals, runtimes,
license counts, private node ownership, and both schedulers' resource traces.
A passing audit does not imply matching schedules.

To compare a rebuilt DR_EVT against an audited native run without rerunning
Slurm, use a fresh output directory:

```sh
python3 validation/slurm/licenses/verify_backfill_retry.py \
  validation/slurm/results/licenses/controlled/performance-on/lassen-prefix2000-hybrid-r32-q64 \
  build/simulator \
  validation/slurm/results/licenses/controlled/backfill-retry-fixed/lassen-prefix2000-hybrid-r32-q64
```

This saves the new schedule, resource curves, checks, source diff, binary
hashes, plotting data, and figures. It reuses the native schedule and its
audit. Use `--skip-plots` if matplotlib is available only in a separate
environment, then run the copied `plot.py` against the new `report.json`.

The corrected 2,000-job results are in
`results/licenses/controlled/dr-evt-performance-on-20261108/`:
`lassen-prefix2000-hybrid-r32-q64/` compares with license planning enabled,
and `bf-licenses-off/lassen-prefix2000-hybrid-r32-q64/` compares with it
disabled. Both reuse audited native Slurm schedules and rerun DR_EVT.
The enabled case includes five DR_EVT process timings in `performance.json`.
The corrected schedules have 1,266 differing starts with planning on and
1,943 with it off. Both four-job scenarios were rerun under `controls/`.

To measure DR_EVT separately after producing a fresh comparison:

```sh
python3 validation/slurm/licenses/measure_dr_evt.py path/to/result --runs 5
```

The timer includes process launch, input, simulation, output and exit;
it excludes Python schedule parsing, auditing and plotting. Every timed
schedule and resource trace is checked against the comparison run.

The first remaining enabled divergence is job 605 at an exact reservation
endpoint. Reconstruct and plot its evidence with:

```sh
python3 validation/slurm/licenses/diagnose_reservation_boundary.py path/to/result path/to/diagnostics
python3 validation/slurm/licenses/plot_reservation_boundary.py path/to/diagnostics/first-divergence-data.json
```

The earlier skipped-tick figure is retained under
`divergence/skipped-tick-before-fix/`. The endpoint analysis isolates the
first remaining difference; it does not explain every later differing start.

The endpoint fix results supersede these earlier comparisons and are in
`results/licenses/controlled/endpoint-fixed/`. With license planning enabled,
all 2,000 DR_EVT start/completion times and resource curves now match native
Slurm exactly. The disabled comparison remains a different reservation policy.
Both four-job scenarios were rerun and retain their previous schedules.

`results/licenses/controlled/endpoint-rule-experiment/` stores the independent
reference experiment: the pre-fix reference reproduces DR_EVT exactly;
correcting admission alone leaves 806 differing starts; correcting both
admission and reservation placement leaves none. The C++ rerun independently
confirms that result. Reproduce from the saved half-open reference with:

```sh
python3 validation/slurm/licenses/compare_endpoint_rules.py path/to/saved/report.json path/to/fresh/output --reference path/to/saved/baseline/reference.py
python3 validation/slurm/licenses/plot_endpoint_comparison.py path/to/fresh/output/endpoint-comparison.json
```

The old job-605 diagnostic remains historical evidence. The endpoint-corrected
periodic hybrid/conservative scheduler retains event-driven FCFS dispatch;
it changes both fit searches while preserving actual runtimes and half-open
capacity reservations. Other scheduling modes retain their existing fit rule.

To regenerate the six-panel plot or audit a saved result:

```sh
python3 path/to/result/plot.py path/to/result/plot-input.json
python3 validation/slurm/licenses/audit_event.py path/to/result
```

Historical EPFL simulator results remain in `results/controlled/` and
`evidence/`. Their setup is preserved in [EPFL_REFERENCE.md](EPFL_REFERENCE.md)
and `run_epfl.sh`; the default runner uses native Slurm.
