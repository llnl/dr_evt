# Virtual time in the native Slurm harness

The harness runs the real `slurmctld` on a virtual clock. It moves the clock
directly from one event to the next, so a job never runs for its real duration.
A run still takes minutes, because each step waits for real requests and native
scheduling work.

## Virtual clock

`event_trace.py` preloads [`clock.c`](licenses/clock.c) into `slurmctld` and the
validation client with `LD_PRELOAD`. The processes share one memory-mapped clock
file. The library replaces these calls:

| Call | Behavior |
|---|---|
| `time()` | Returns the virtual time in whole seconds. |
| `gettimeofday()` | Returns the virtual second with a strictly increasing subsecond counter, so `sched_min_interval=0` still permits dispatch. |
| `clock_gettime(CLOCK_REALTIME)` | Returns the virtual second. |
| `sleep()` | Returns after 1 ms of real time. |
| `pthread_cond_timedwait()` | Waits at most 1 ms of real time. |

Native timers therefore take almost no real time. For example, the backfill
agent's `bf_interval=30` wait returns after 1 ms; the agent then compares the
virtual time with its last cycle to decide whether to run.

`CLOCK_MONOTONIC`, other clock IDs, and RPC timeouts stay real
(`MessageTimeout=10` is 10 real seconds).

## Steps

The harness starts the clock at the first submission and repeats one step per
event time. The next time is the earliest of:

- the next job arrival,
- the next completion (start plus actual runtime), and
- the next 30-second backfill tick, anchored to the first submission.

Each step:

1. `TIME` sets the clock and holds native backfill, and with
   `--batch-main-dispatch` also main dispatch, while the event batch is applied.
2. Completion and epilog RPCs release finished jobs; submission RPCs add the
   arrivals.
3. `RELEASE` lets native main dispatch and backfill run.
4. The harness reads native state every 10 ms until it is settled:
   - a snapshot of job states, start times, and license use that is unchanged
     three times in a row;
   - with `--batch-main-dispatch`, one finished main-dispatch pass after any
     state change; and
   - two finished backfill-agent checks, so any backfill cycle that started
     after `RELEASE` has finished.
5. The settled snapshot is audited and recorded, and the clock moves on.

No `slurmd` runs. The build overlay drops job launch and termination messages
to compute daemons and disables node pings while the validation clock is
active. The harness supplies the completion and epilog RPCs instead.
[`build_event.sh`](licenses/build_event.sh) records each overlay and its reason
with every run.

## Where real time goes

One local Docker run of the 2,000-job capacity case took 7,527 steps to cover
40.6 hours of virtual time, in 425 seconds of real time: about 56 ms per step.
That time is spent on:

- RPC round trips to `slurmctld` for each event, snapshot, and acknowledgement;
- the settling check, which takes at least 40 ms per step; and
- native main dispatch and backfill.

Each report's `slurm_metrics.process_elapsed_seconds` covers the whole replay,
including controller startup and node registration. It is not a measure of
scheduling time alone.

## Sources

- [`licenses/clock.c`](licenses/clock.c): virtual clock, sleeps, and gates
- [`licenses/client.c`](licenses/client.c): `TIME`, `RELEASE`, `DISPATCH`,
  `BACKFILL`, and `DUMP` commands
- [`licenses/build_event.sh`](licenses/build_event.sh): native source overlays
- [`licenses/event_trace.py`](licenses/event_trace.py): step loop and audits
