# Simulation Validation

Native comparisons test DR_EVT against external schedulers on recorded workloads
and small controls. Each subpage gives the resource model, scheduler settings,
results and processing times, divergence analysis, and reproduction commands.

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

DR_EVT models interchangeable capacity counts rather than specific-node
placement. The Slurm and OpenPBS comparisons remove competition for node
identities while preserving shared-capacity constraints. Fiction's Fluxion
comparison retains node matching and specific-node reservations.

Matches apply to the tested workloads and configurations. They do not establish
general scheduler equivalence. Small controls isolate particular differences;
the cause of every Fluxion trace difference has not been identified. Slurm's
uncontrolled main-versus-backfill order has produced differences in another run.

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
| Fiction FCFS | 0.020736 s | 145.849 s | 7,033.47× |
| Fiction hybrid | 0.075825 s | 376.8174 s | 4,969.57× |
| Slurm protected | 0.037473 s | 697.8357 s | 18,622.15× |
| Slurm no-reservation | 0.012943 s | 739.2877 s | 57,120.76× |
| OpenPBS FCFS | 0.182249 s | 315.908234 s | 1,733.39× |
| OpenPBS grouping | 7.257180 s | 325.310732 s | 44.83× |

Speedup is native processing time divided by the DR_EVT median, calculated
from unrounded measurements. Timing covers process execution, including input
and output. Native runs also include scheduler-specific validation work such
as startup, RPCs, synchronization, and resource snapshots. The ratios compare
these measured processes; they do not isolate scheduling-algorithm performance.
Each scheduler subpage describes its timing scope.

**DR_EVT models only a subset of scheduler behavior:** the configured queue
ordering, backfill, reservations, and timing rules over interchangeable capacity
counts. It does not reproduce the full native resource model, node placement,
fairshare, or production server and compute-node operations. The speedups apply
to these configured simulations, not to an equivalent implementation of every
native scheduler feature.

The [measurement evidence](../_static/scheduler-validation/performance/README.txt)
provides all DR_EVT runs, native hybrid timing, benchmark inputs, build details,
and reproduction commands. Every repeated DR_EVT schedule matches the
published result.
