# Simulation Validation

Native comparisons test DR_EVT against external schedulers on recorded workloads
and small controls. Each subpage gives concise results, explains the relevant
options, and provides reproduction commands.

**DR_EVT models a subset of scheduler behavior.** Capacity mode models
interchangeable counts. Placement mode additionally models exact homogeneous
whole-node identities. Both omit many production policies and server operations,
so agreement applies only to the tested workloads and settings.

| Native scheduler and configuration | DR_EVT priority policy and backfill | Recorded result for 2,000 Lassen jobs |
|---|---|---|
| [Flux-Fiction](FLUX_VALIDATION.md): Fluxion, hybrid backfill | `fcfs`, event-driven hybrid backfill | Different starts; a four-job example isolates specific-node reservations |
| [Slurm](SLURM_VALIDATION.md): capacity with `bf_licenses`; default-Slurm placement | `fcfs`, periodic hybrid backfill | Placement: 0 start and 0 node differences; capacity: 2 order-dependent start differences |
| [OpenPBS](OPENPBS_VALIDATION.md): strict FCFS with one-second forecast groups | `fcfs_1s_evt_group`, event-driven hybrid backfill | Capacity: 0 schedule differences; placement: 0 schedule and 0 node differences |

```{toctree}
:maxdepth: 1

FLUX_VALIDATION
SLURM_VALIDATION
OPENPBS_VALIDATION
```

Capacity comparisons give each native job a private dummy node or vnode while a
shared consumable resource represents interchangeable capacity. Placement
comparisons use one shared homogeneous node pool and compare exact identities.
The scheduler pages keep these cases separate.

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

The checked-in `validation/fixtures/jobs.csv` is the integer-second, absolute
Unix-time input used by the current native adapters. Expected native schedules
use the same clock and are stored beside it. The original LAST extraction is not
checked in; its interval and hash are retained for provenance and regeneration.
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
