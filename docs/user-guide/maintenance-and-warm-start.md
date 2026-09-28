# Maintenance, Capacity Changes, and Warm Starts

Historical job traces can contain periods in which the whole machine is down,
only part of it is available, or the workload represented by the trace is
paused while another queue uses the hardware. DR_EVT can model the capacity
available to the simulated workload and initialize a mid-trace run from the
jobs that were active at its starting time.

## Capacity schedule

Pass `--capacity_schedule` a CSV with these columns:

```text
time,total_nodes
1713139200,0
1713160800,120000
1713225600,158976
```

Rows are change points and must have strictly increasing times. The first row
selects epoch or calendar encoding for the whole file; `time` accepts the same
forms as job traces. Capacity is
`--total_nodes` before the first row and each value remains effective until
the next row. A row at time zero therefore replaces the initial available
capacity immediately. Values must be between zero and `--total_nodes`.

`--total_nodes` is the physical maximum, even when a capacity schedule is
present. It—not the currently scheduled capacity—is the oversized-job
rejection threshold. A job larger than the current scheduled capacity but no
larger than `--total_nodes` waits for a later capacity increase instead of
being rejected.

```bash
simulator warm_start.csv \
  --trace_format simple --timestamp_format epoch \
  --run_time_mode actual --total_nodes 158976 \
  --capacity_schedule capacity.csv
```

Reservations anticipate the schedule. A projected release timeline merges
running-job releases with the scheduled changes still ahead, so a waiting job
that fits only after an increase is reserved at that increase, a reduction
before a projected release pushes the reservation later, and a backfill
candidate is tested against that reservation. A waiting job that no release
or scheduled change can ever satisfy holds no reservation and does not block
backfill behind it. The reported FCFS-head shadow time is `-1` in that case.

Capacity reductions are non-preemptive. Existing jobs retain their nodes and
finish normally; no new job starts until it fits within current capacity. The
resource trace reports zero free nodes while a reduction leaves the system
temporarily overcommitted. Capacity increases are scheduling events, so
waiting jobs are reconsidered immediately even if no job arrives or completes
at that time.

Instantaneous and time-accounted utilization use scheduled capacity rather
than the physical `--total_nodes` maximum. During a non-preemptive drain,
effective capacity is the larger of scheduled capacity and live allocation;
this excludes unavailable nodes without reporting utilization above 100%.

A scalar capacity schedule describes the resources available to the workload
being simulated. To model a normal queue paused during a split-queue
maintenance period, use capacity zero for the normal workload even when jobs
in another queue continue running. Queue-specific partitions and job routing
are not encoded by this format.

Protobuf configuration uses the corresponding field:

```text
total_nodes: 158976
capacity_schedule: "capacity.csv"
```

## Detect candidate periods

`scripts/detect_capacity_periods.py` sweeps a historical replay trace and
finds sustained periods with a waiting backlog and either low allocation or
no starts. For example:

```bash
python3 scripts/detect_capacity_periods.py historical.csv candidates.csv \
  --total-nodes 158976 \
  --utilization-threshold 0.25 \
  --minimum-duration 21600 \
  --capacity-schedule capacity.csv
```

Repeat `--queue-id ID` to restrict allocation, starts, and backlog detection
to the normal queue or queues. Without it, all jobs are included. The proposed
capacity is the peak observed allocation in each candidate period plus five
percent headroom; change that factor with `--capacity-headroom`.

For a more comprehensive analysis, use the
`scripts/detect_queue_pause/` toolkit. It reconstructs allocation and pending
demand in configurable time bins, handles multiple trace files, distinguishes
queue pauses, shutdowns, and reduced-capacity periods, and can incorporate an
EASY-backfill opportunity audit. It is still heuristic: the additional
evidence improves classification but does not prove that maintenance or a
queue pause occurred.

Neither tool is proof of maintenance. A job trace alone cannot distinguish
maintenance from insufficient demand, reservations, dependencies, or scheduler
policy. Review the generated candidates and external maintenance records before
using either capacity schedule.

## Replay-based warm start (recommended)

The simulator can consume a replay-format historical schedule directly. Warm
start records always include their historical `begin_time` and `end_time`. For
example, the replay-based warm-start test trace contains:

```text
job_submit_time,begin_time,end_time,num_nodes,time_limit
0,0,20,40,20
10,30,80,60,50
20,60,65,10,5
50,50,70,40,20
55,60,90,60,30
```

```bash
simulator tests/test_traces/feature/warm_start_native.csv \
  --trace_format simple --timestamp_format epoch \
  --sim_start_time 50 --run_time_mode actual --total_nodes 100
```

Jobs with `begin_time < t` bypass the wait queue. The simulator replays them
only far enough to reconstruct live occupancy and policy state at `t`, then
discards all earlier resource samples and resets resource-area accounting.
Those seed jobs retain their historical end events so their nodes remain busy
after `t`, but their records are suppressed before completion and therefore
never enter job output or statistics. Jobs submitted at or after `t` are
rescheduled through the normal wait queue; a job beginning exactly at `t` is
not classified as historical.

Jobs submitted before `t` but not yet started are inherited into the waiting
queue by default. They retain their historical submit times, so their reported
wait includes time before the boundary and their FCFS order is preserved.
Use `--warm_start_wait_queue drop` to retain the legacy behavior of omitting
them. Historical scheduler reservation state is not reconstructed, so the
first conservative or hybrid backfill decision can still differ from history.

Here `t=50` is the global simulation boundary selected by
`--sim_start_time 50`. It is not a per-job `begin_time`. The first
job is warmup history that finishes before `t`. The second starts before `t`
and remains active until its historical end at `80`. The third was submitted
before `t` but had not started, so it is inherited. The last two are submitted
at or after `t` and go through the scheduler.

Warm start and runtime selection are separate concerns. `--sim_start_time`
classifies the warmup records; every such record always uses its historical
`begin_time` and `end_time`. `--run_time_mode actual` in this command is an
independent choice for the jobs simulated by the scheduler. Those ordinary
jobs follow the same `actual`, `limit`, or `distribution` runtime selection as
any non-warm-start simulation before they enter the wait queue.

During this temporary warm stage, completion processing checks whether the
job began before `t`. Once the final historical job departs, execution changes
to the ordinary simulation stage, whose compiled event loop contains no such
check. Ordinary jobs can still be scheduled while a warmup job is active; the
two stages select event-processing paths rather than imposing a scheduling
barrier. Jobs submitted before `t` but not yet running are intentionally
excluded because inheriting a historical wait queue is a separate scheduling
policy choice.

### Current limitation: historical waiting jobs are not restored

Replay warm start reconstructs jobs that were running at the boundary, but it
does not reconstruct jobs submitted before `t` that were still waiting then.
It therefore cannot reproduce the historical pending queue, its ordering, or
pre-boundary wait durations.

Restoring those jobs is future work. It must preserve the boundary-time
resource and power state of running jobs while restoring waiting jobs in a
well-defined order under the active capacity schedule. Until then, warm start
models the live allocation at `t`, not the complete historical queue state.

### Synthetic initialization traces (legacy workaround)

`prepare_warm_start_trace.py` is an alternative workaround for environments
that cannot use the recommended replay-based `--sim_start_time` path. It
creates an ordinary simulation-format trace containing synthetic initialization
jobs. It selects every historical job satisfying
`begin_time <= t < end_time`. A job beginning exactly at `t` is already active;
a job ending at `t` is complete. The helper submits initialization jobs at `t`
with both `actual_run_time` and `time_limit` set to `end_time - t`, then appends
future workload arrivals.

```bash
python3 scripts/prepare_warm_start_trace.py \
  historical_schedule.csv warm_start.csv \
  --start-time 1713139200 \
  --workload-trace simulation_input.csv \
  --total-nodes 158976
```

The simulator does not interpret the output's `initialization` or
`source_job_index` columns; they are provenance only. Initialization records
enter the ordinary wait queue, scheduler, output, and statistics. Run this
trace as an ordinary simulation with `--run_time_mode actual`, without
simulator `--sim_start_time`. Initialization rows sort before ordinary jobs
submitted at the same timestamp so they normally establish the initial
allocation first.

`--run_time_mode limit` happens to give initialization records the same
remaining duration because their two runtime fields are equal, but it also
changes every future ordinary job to use its requested limit.
`--run_time_mode distribution` resamples initialization durations and therefore
does not preserve their historical departures. Because runtime mode is global,
this workaround cannot protect historical departures independently of future
job runtime selection as the replay-based method does.

The helper reports the initialization job and node counts and can reject a
state larger than `--total-nodes`. The capacity schedule effective at `t` must
also be at least that large, or the scheduler cannot start all initialization
jobs at `t`.

The helper remains available when a standalone simulation-format artifact is
required, but it is not the recommended warm-start method. Its initialization
records are ordinary output jobs, unlike replay-based `--sim_start_time` seed
jobs, which are deliberately omitted from job output and statistics.
