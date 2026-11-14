# Simulation Validation

Native comparisons test DR_EVT against external schedulers on recorded workloads
and small controls. Each subpage gives the resource model, scheduler settings,
results and processing times, divergence analysis, and reproduction commands.

**DR_EVT models a subset of scheduler behavior:** queue ordering, backfill,
reservations, and timing rules over interchangeable capacity counts. It omits
native resource graphs, node placement, fairshare, and production server and
compute-node operations. Agreement applies to the tested workloads and settings.

| Native scheduler and configuration | DR_EVT priority policy and backfill | Recorded result for 2,000 Lassen jobs |
|---|---|---|
| [Flux-Fiction](FLUX_VALIDATION.md): Fluxion, hybrid backfill | `fcfs`, event-driven hybrid backfill | Different starts; a four-job example isolates specific-node reservations |
| [Slurm](SLURM_VALIDATION.md): periodic backfill with pooled capacity, `bf_licenses=on` | `fcfs`, periodic hybrid backfill | All starts and completions match in the recorded license-on run; main-versus-backfill order is uncontrolled |
| [OpenPBS](OPENPBS_VALIDATION.md): strict FCFS ordering with backfill and shared capacity | `fcfs_1s_evt_group`, event-driven hybrid backfill | All starts and completions match on the original converted input |

```{toctree}
:maxdepth: 1

FLUX_VALIDATION
SLURM_VALIDATION
OPENPBS_VALIDATION
```

The historical capacity comparisons give each Slurm/OpenPBS job a private
dummy node or vnode, suppressing
competition for node identities while retaining shared-capacity constraints.
The native schedulers still assign nodes. Production resource binding can
therefore cause further divergence, as the
[Fiction comparison with Fluxion](FLUX_VALIDATION.md#minimal-reproducer-for-backfill-start-time-differences)
demonstrates.

The native runners also provide a separate placement campaign. In that mode,
Slurm/OpenPBS schedule whole-node requests on one shared homogeneous node pool,
and the reports compare exact native node identities with DR_EVT placement
allocations as well as starts, completions, resource bounds, and process time.
Capacity and placement results are kept in separate directories so historical
laptop timings are not reused in a host-specific campaign. With Slurm's
30-second backfill timer, R=32, and Q=64, DR_EVT placement matches native
Slurm's start times and node allocations for all 2,000 jobs; see the
[Slurm placement agreement](https://github.com/LLNL/dr_evt/tree/main/validation/slurm#exact-placement-agreement)
for the reproduced node-selection rules and their scope.

See [Scheduling Loops](SCHEDULER_LOOPS.md) for policy rules,
[Realism and Known Gaps](REALISM_AND_GAPS.md) for model limits, and the
[Testing Guide](../TESTING_GUIDE.md) for regression and CI coverage.
Source attribution and upstream licenses are listed in
[Scheduling Loops](SCHEDULER_LOOPS.md#sources).

## Lassen workload

All published comparisons use the same **2,000 recorded jobs**, extracted
from the Lassen dataset published in [LLNL's LAST repository](https://github.com/LLNL/LAST).
The inclusive submission-time interval is **2020-08-19 04:29:39 PDT** through
**2020-08-19 15:38:34 PDT**, corresponding to numeric timestamps
**1597836579.000** through **1597876714.000**. The latest recorded completion
is **2020-08-20 21:06:09.138 PDT** (**1597982769.138**); simulated schedules
can finish at different times. Calendar times use `America/Los_Angeles`
(UTC−7 for this interval).

The local input `lassen-756-01.csv` is an extraction from that dataset.
It is not checked into this repository.
To reproduce the workload, use the submission-time interval above to identify
the 2,000 jobs in LAST and retain their arrivals, node counts, time limits,
and recorded durations. The runners select the earliest 2,000 jobs from the
supplied extraction in stable arrival order, retaining input order for ties.
The filename alone does not identify the workload, and `--max-jobs 0` selects
the entire supplied CSV rather than the published 2,000-job workload.

Each comparison starts with an empty **756-node `pbatch` capacity pool**.
Each scheduler subpage describes its resource representation and replay
timestamp conversion.

## Processing time and speedup

All DR_EVT cases use the same Release binary and Linux Docker environment on
an **Apple M3 Max**. DR_EVT times are medians of five runs after one untimed
warmup; native times are single instrumented runs.

Bold rows are the three main comparisons; the remaining rows are controls
or diagnostics.

| Comparison | Reference processing time | DR_EVT median of 5 runs | Speedup |
|---|---:|---:|---:|
| Flux-Fiction FCFS (`sched-simple`) | 145.849 s | 0.020419 s | 7,142.92× |
| **Flux-Fiction hybrid (Fluxion)** | **376.8174 s** | **0.061328 s** | **6,144.30×** |
| **Slurm protected** | **697.8357 s** | **0.036436 s** | **19,152.30×** |
| Slurm no-reservation | 739.2877 s | 0.012205 s | 60,571.50× |
| OpenPBS FCFS | 315.908234 s | 0.059443 s | 5,314.48× |
| **OpenPBS grouping** | **325.310732 s** | **0.190654 s** | **1,706.29×** |

Speedup is native processing time divided by the DR_EVT median, calculated
from unrounded measurements. Timing covers process execution, including input
and output. Native runs also include scheduler-specific validation work such
as startup, RPCs, synchronization, and resource snapshots. The ratios compare
these measured processes; they do not isolate scheduling-algorithm performance.
Each scheduler subpage describes its timing scope.

The [measurement evidence](../_static/scheduler-validation/performance/README.txt)
provides all DR_EVT runs, native hybrid timing, benchmark inputs, build details,
and reproduction commands. Every repeated DR_EVT schedule matches the
published result.
