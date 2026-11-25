# Virtual time in the native OpenPBS harness

The harness runs the unmodified OpenPBS scheduler library in one process with a
virtual clock. It moves the clock directly from one event to the next, so a job
never runs for its real duration. No server, database, MOM daemon, or network
connection is involved.

## Virtual clock

[`link.py`](link.py) links [`reference.cpp`](reference.cpp) with the native
scheduler library, `libpbs_sched.a`, and replaces these calls with
`-Wl,--wrap`:

| Call | Replacement |
|---|---|
| `time()` | Returns the driver's current virtual time. |
| `pbs_statserver`, `pbs_statque`, `pbs_statvnode`, `pbs_selstat`, `pbs_statresv`, `pbs_statsched`, `pbs_statrsc` | Return an emulated server, queue, vnodes, jobs, and resources. |
| `pbs_asyrunjob` | Records a native launch. |
| `pbs_asyalterjob` | Records native estimated start times. |
| `pbs_geterrmsg`, `get_sched_cmd_noblk` | Report no error and supply scheduling commands. |

The scheduler sources read time only through `time()`. Native selection, vnode
placement, and future-capacity planning are unchanged. The build manifest
records the wrapped symbols and library hashes.

## Epoch

The driver adds a fixed epoch, `1700000000`, to every input time. Its input must
therefore start near zero. `compare.py` writes native input relative to the
first submission and adds that origin back to native times before comparison;
each report records it as `native_time_origin`.

## Steps

At each event time, the driver:

1. completes jobs whose start plus actual runtime equals the current time, and
   makes jobs submitted at that time visible to the emulated server;
2. runs one native `scheduling_cycle()` if any job is waiting or running, after
   all arrivals and completions at that time are applied;
3. records a capacity snapshot; and
4. moves the clock to the next arrival or actual completion.

There is no periodic timer, polling, or sleep. Native forecasts use requested
walltime, so an early completion triggers replanning at that completion.

## Where real time goes

One local Docker run of the 2,000-job workload took 2,836 scheduling cycles in
312 seconds for capacity (about 110 ms per cycle) and 2,853 cycles in 288 seconds
for placement. Each cycle queries the emulated server, rebuilds the scheduler's
view of the queue and 756 vnodes, and computes its forecasts.

Each report's `openpbs_wall_seconds` covers the whole driver process, including
startup, input, and schedule and event output. It is not a measure of
scheduling time alone.

## Sources

- [`reference.cpp`](reference.cpp): emulated server, clock, and event loop
- [`link.py`](link.py): wrapped symbols and native build manifest
- [`compare.py`](compare.py): input conversion, time origin, and audits
