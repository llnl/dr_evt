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

To regenerate the six-panel plot or audit a saved result:

```sh
python3 path/to/result/plot.py path/to/result/plot-input.json
python3 validation/slurm/licenses/audit_event.py path/to/result
```

Historical EPFL simulator results remain in `results/controlled/` and
`evidence/`. Their setup is preserved in [EPFL_REFERENCE.md](EPFL_REFERENCE.md)
and `run_epfl.sh`; the default runner uses native Slurm.
