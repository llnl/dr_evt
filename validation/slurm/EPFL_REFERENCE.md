# Historical EPFL Slurm simulator comparison

This harness compares DR_EVT's event-driven FCFS with separate periodic
backfill against the native Slurm scheduler in `external/slurm_simulator`.
The checked-out simulator is based on Slurm 17.11.7. Main-scheduler minimum
interval and batching delays are disabled: `sched_min_interval=0`,
`batch_sched_delay=0`, and no `defer`. Backfill remains periodic.

## Reproduce

From the repository root, run the small paired controls:

```sh
bash validation/slurm/run_epfl.sh
```

Include the same chronological 2,000-job Lassen prefix used by the Fiction
validation:

```sh
SLURM_VALIDATION_TRACE=/dr_evt_source/lassen_traces/lassen-756-01.csv \
    bash validation/slurm/run_epfl.sh
```

Docker builds Linux dependencies and both schedulers. Source mounts are
read-only; exports and binaries live in a reusable Docker volume. Set
`SLURM_VALIDATION_BUILD_VOLUME` to a fresh name when changing Slurm revisions
or configure options. `SLURM_VALIDATION_RESULTS` selects a fresh host output
directory; the default uses a timestamp under `results/`. The raw trace and
generated results are not committed.

The companion `slurm_sim_tools` repository was consulted for setup, but is
not required to run this harness. No Slurm database or compute-node daemon is
needed: this simulator submits jobs and completes their allocations internally.
Munge runs inside the container for the native credential plugin.

## Compared configuration

Both engines use an initially empty, homogeneous whole-node pool, one core
per node, stable FCFS order, and no node-sharing or node-release delay.

| Control | DR_EVT | Slurm simulator |
|---|---|---|
| Event-driven main dispatch | periodic-backfill event path | main scheduler; minimum interval and batching delay zero |
| Backfill interval | `backfill_interval=30` | `bf_interval=30` |
| Reservations | `reservation_depth=32` | `bf_job_part_count_reserve=32`, one partition |
| Backfill examination depth | `backfill_queue_depth=64` | `bf_max_job_part=64` |
| Planning resolution | `planning_time_resolution=1` | `bf_resolution=1` |
| Planning horizon | `planning_horizon=259200` seconds | `bf_window=4320` minutes |
| Main queue depth | no additional cap | `default_queue_depth=100000` |

This simulator's `bf_max_job_test` implementation bounds resource-time table
size, rather than enforcing a placement-test cap. The harness sets it to
4096, above the per-partition candidate limit, and uses `bf_max_job_part`
for the candidate cap. This is a comparison of the listed configurations;
the counters and reservation placement rules are not assumed equivalent.
Slurm also runs a full main-scheduler pass every 60 seconds.

DR_EVT's `per_pass_accounting` option is restricted to event-driven scheduling
and is omitted for periodic backfill. The default resource-time reservation
rule is used (`DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`).

The simulator trace ABI accepts integer-second durations and whole-minute
limits. The selected Lassen arrivals are integral seconds and limits are
already whole minutes. Recorded durations are rounded **up to whole seconds
in both inputs**; `workload.json` records the conversion, source hash, and
selected source rows. This workload differs from the millisecond-preserving
Fiction comparison by that explicit runtime conversion.

## Evidence and limits

Each case retains exact commands, native configuration, job-completion
records, resource snapshots, DR_EVT's job/resource CSVs, and `report.json`.
The report checks job counts, arrival constraints, exact input runtimes,
capacity, node ownership, and agreement of resource-manager curves with
job records. It reports start-time differences and integrates the area
between step curves over a shared observation window.

Nine small controls assert immediate submission/completion dispatch,
backfill on the periodic timer, protection of a waiting job, and the
four-job assignment difference. Expected differences are asserted explicitly;
passing the harness does not mean every compared schedule is identical.

The checked 2,000-job run has 1,808 differing starts and a resource-curve
difference of 2,372,872 node-seconds (2.1431% of the shared capacity-time
window). Mean waiting times are 10.4462 hours for DR_EVT and 10.6180 hours
for Slurm; makespans are 40.6817 and 40.6736 hours, respectively. All job,
capacity, runtime, node-ownership, and curve audits pass. The native run
captures 2,499 resource snapshots; all 3,817 recorded backfill cycles stay
within the 64-job placement cap. Compact checked evidence is in [evidence/](evidence/).

`slurm-source.json` records every overlay applied to the exported simulator:
ARM libc discovery, the modern `gettimeofday` declaration, end-of-file trace
handling, and initialization of the native backfill configuration. The
configuration fix is needed because the simulator bypasses the ordinary
backfill-agent startup and reads an uninitialized local flag. Effective
settings are logged and checked before results are accepted.
The read-only observer runs after a simulator event/scheduling batch and
does not request scheduling passes. Native allocation and reservation
methods are retained.

The unmodified simulator's clock advances with host execution as well as
its configured step, which can delay completions and shift backfill ticks.
The validation export adds opt-in `SLURM_VALIDATION_CONTROLLED_CLOCK`: host
execution and clock reads do not advance simulated time. The controller's
one-second step advances it instead, and the backfill wake accepts the exact
deadline. Integer arrivals and runtimes are therefore preserved. Extra modeled
scheduler cost is disabled with `scalingFactor=0` and zero launch/check
increments. These clock changes are recorded alongside the other overlays.
Process runtime includes native startup and simulation work. The results
concern this simulator revision and configuration, rather than current
production Slurm generally.

To regenerate figures from a saved report, with Matplotlib installed:

```sh
python3 validation/slurm/plot.py path/to/report.json
```

This writes the six-panel `comparison-metrics.pdf` (waiting-time summary and
distribution, utilization, makespan, resource area, and waiting-time differences),
along with PNG and SVG versions. The resource-curve overview is saved as
`comparison.pdf`, PNG, and SVG.
