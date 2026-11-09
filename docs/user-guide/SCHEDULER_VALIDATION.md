# Simulation Validation

Native comparisons test DR_EVT against external schedulers on recorded workloads
and small controls. Each subpage gives the resource model, scheduler settings,
results and processing times, divergence analysis, and reproduction commands.

**DR_EVT models a subset of scheduler behavior:** queue ordering, backfill,
reservations, and timing rules over interchangeable capacity counts. It omits
native resource graphs, node placement, fairshare, and production server and
compute-node operations. Agreement applies to the tested workloads and settings.

| Comparison | Recorded result for 2,000 Lassen jobs |
|---|---|
| [Flux through Fiction](FLUX_VALIDATION.md), FCFS with `sched-simple` | All starts and completions match; Fluxion is unloaded |
| [Flux through Fiction](FLUX_VALIDATION.md), hybrid backfill with Fluxion | Different starts; a four-job example isolates specific-node reservations |
| [Slurm](SLURM_VALIDATION.md), periodic hybrid backfill with pooled capacity | All starts and completions match in the recorded license-on run; main-versus-backfill order is uncontrolled |
| [OpenPBS](OPENPBS_VALIDATION.md), DR_EVT `fcfs_1s_evt_group` | All starts and completions match on the original converted input |
| [OpenPBS](OPENPBS_VALIDATION.md), default DR_EVT `fcfs` | 1,433 jobs have different starts or completions |

```{toctree}
:maxdepth: 1

FLUX_VALIDATION
SLURM_VALIDATION
OPENPBS_VALIDATION
```

Slurm and OpenPBS give each job a private dummy node or vnode, suppressing
competition for node identities while retaining shared-capacity constraints.
The native schedulers still assign nodes. Production resource binding can
therefore cause further divergence, as the
[Fiction comparison with Fluxion](FLUX_VALIDATION.md#minimal-reproducer-for-backfill-start-time-differences)
demonstrates.

See [Scheduling Loops](SCHEDULER_LOOPS.md) for policy rules,
[Realism and Known Gaps](REALISM_AND_GAPS.md) for model limits, and the
[Testing Guide](../TESTING_GUIDE.md) for regression and CI coverage.
Source attribution and upstream licenses are listed in
[Scheduling Loops](SCHEDULER_LOOPS.md#sources).

## Processing time and speedup

Workload: **2,000 jobs** from the [Lassen trace](https://github.com/LLNL/LAST),
submitted from **2020-08-19 04:29:39 PDT** through
**2020-08-19 15:38:34 PDT**, using an initially empty
**756-node `pbatch` partition**. These calendar times display the supplied
CSV's numeric timestamps in Pacific daylight time (`America/Los_Angeles`,
UTC−7). Each scheduler subpage describes its replay timestamp conversion.

All DR_EVT cases use the same Release binary and Linux Docker environment on
an **Apple M3 Max**. DR_EVT times are medians of five runs after one untimed
warmup; native times are single instrumented runs.

| Comparison | DR_EVT median of 5 runs | Native processing time | Speedup |
|---|---:|---:|---:|
| Fiction FCFS | 0.020419 s | 145.849 s | 7,142.92× |
| Fiction hybrid | 0.061328 s | 376.8174 s | 6,144.30× |
| Slurm protected | 0.036436 s | 697.8357 s | 19,152.30× |
| Slurm no-reservation | 0.012205 s | 739.2877 s | 60,571.50× |
| OpenPBS FCFS | 0.059443 s | 315.908234 s | 5,314.48× |
| OpenPBS grouping | 0.190654 s | 325.310732 s | 1,706.29× |

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
