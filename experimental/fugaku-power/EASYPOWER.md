# Private EASYPower workflow

These files are intentionally untracked while the experiment is under
development. The public CMake configuration does not name or build EASYPower.

Build the private driver through the untracked CMake injection:

```bash
experimental/fugaku-power/scripts/build_easypower_experiment.sh
```

The default executable is
`experimental/fugaku-power/build/easypower_experiment` on the shared repository
filesystem. Set `EASYPOWER_BUILD_DIR` to use another shared build directory, or
pass ordinary CMake cache arguments to the build script. The Slurm workflow
defaults to this prebuilt shared executable (`BUILD_DRIVER=0`) and never relies
on node-local `/tmp`. Set `EASYPOWER_DRIVER=/shared/path/easypower_experiment`
to select a different executable, or set `BUILD_DRIVER=1` to rebuild before the
sweep. `EASYPOWER_DRIVER` is an exact file path, whereas
`EASYPOWER_BUILD_DIR` is the directory used for private builds. Compilation
uses four parallel jobs by default; override that with
`EASYPOWER_BUILD_JOBS`.

The private build uses `gcc/13.3.1-magic`, `openmpi/4.1.2`, and the matching
`boost/1.86.0`. It deliberately unloads `python/3.13.2` during CMake configure
and linking because that module exposes an ABI-incompatible Anaconda Boost;
the Slurm workflow reloads Python afterward. gRPC, Protobuf, Python bindings,
Ser20, and unit tests are explicitly disabled for this private target build.

Run a sweep:

```bash
experimental/fugaku-power/scripts/run_easypower_sweep.py \
  TRACE_1.csv TRACE_2.csv \
  --total-nodes 142167 \
  --candidate-job-windows 16,32,64,128,256 \
  --candidate-time-windows unlimited,1h,6h \
  --maximum-power 12000000 \
  --output-dir /tmp/easypower-sweep
```

Each positional trace is an independent workload. For progressive loading,
put the ordered batch paths in a text file (one path per line) and pass that
list instead:

```bash
experimental/fugaku-power/scripts/run_easypower_sweep.py \
  --infile-list TRACE_BATCHES.txt \
  --total-nodes 142167 \
  --candidate-job-windows 16,32,64,128,256 \
  --candidate-time-windows unlimited,1h,6h \
  --maximum-power 12000000 \
  --capacity-schedule CAPACITY.csv \
  --output-dir /tmp/easypower-progressive-sweep
```

The list is one combined workload: the runner creates one EASY baseline, then
runs every Cartesian-product pair of candidate job window and candidate time
window across the whole ordered sequence. Relative paths inside the list use
the repository root because that is the simulation working directory. The
configuration records both the list path and the resolved batch paths.

The optional capacity schedule is a CSV with `time,total_nodes` columns. The
configured `--total-nodes` remains the physical maximum and every scheduled
capacity must be at most that value. Capacity reductions do not preempt
running jobs. EASYPower's forward horizon follows future capacity changes, and
reported utilization divides allocated-node time by effective-capacity time
(`max(scheduled capacity, live allocation)` while reductions drain).

The endpoint metric compares instantaneous power at the two ends of a physical
interval: `abs(P(t + delta) - P(t)) / delta`. It is not a fixed-bin average and
is unrelated to the scheduler candidate windows. Pass `--endpoint-windows ''`
to disable endpoint analysis.

## Slurm fiscal-half sweep

The private `scripts/submit_easypower_fiscal_halves.slurm` template uses the
Japanese fiscal year: H1 is April--September and H2 is October--March. It
discovers monthly files named `YY_MM_scheduling_trace.csv` and retains partial
edge periods. As a deliberate leading-boundary exception, an initial March
2021 file is prepended to `FY2021-H1`, making that first workload March through
September 2021. Later halves follow the normal Japanese FY boundaries.
For every half, it runs
`scripts/detect_queue_pause/generate_capacity_schedule.sh` against the matching
historical traces and then supplies the generated queue-pause capacity schedule
to the sweep. The template requests one exclusive six-hour allocation of 35 nodes. Inside
that allocation it starts 35 concurrent, exclusive `srun` job steps: one fiscal
half and one candidate-job window per node. Each step reserves four CPU cores;
the EASY baseline and the three configured submission-time-window simulations
run concurrently on those cores for the first job window of each season. The
other job-window steps run only their three submission-time-window simulations
and reuse the season's shared EASY baseline. Thus EASY runs exactly once per
season. Each step has one Slurm task and uses `--mpi=none`, so no MPI runtime is
launched.

```bash
TRACE_DIR=/path/to/traces_no_times_nonoverlap \
HISTORICAL_TRACE_DIR=/path/to/traces_nonoverlap \
TOTAL_NODES=158976 \
OUTPUT_ROOT=/path/to/results \
sbatch experimental/fugaku-power/scripts/submit_easypower_fiscal_halves.slurm
```

Every submission writes beneath `OUTPUT_ROOT/run-JOB_ID/`; within it, each
step owns `FYyyyy-Hn/job-window-N/`. A repeated submission therefore
gets a new directory, and an element refuses to run if its exact task directory
already exists. This prevents a reused `RUN_ID` or requeued task from silently
overwriting results.

`TRACE_DIR` contains simulation inputs, while `HISTORICAL_TRACE_DIR` contains
same-named traces with `begin_time` and `end_time`, which queue-pause detection
requires. The default `CAPACITY_SCENARIO=queue-pause-only` selects
`resource_capacity_without_reduced_capacity.csv`. Set it to `with-reduced` to
run the inferred reduced-capacity sensitivity case instead. Detection defaults
to `TRACE_TIMEZONE=JST`, `OVERLAP_POLICY=filename-month`, and
`SIMULATOR_FORMAT=1`.

Partial first and last periods are retained automatically, while an incomplete
interior half is rejected by default. Set `ALLOW_PARTIAL_HALVES=1` to retain
partial interior periods too. Optional `FY_START` and `FY_END` filters use
Japanese fiscal-year labels. Candidate windows can be overridden with
`CANDIDATE_JOB_WINDOWS` and `CANDIDATE_TIME_WINDOWS` in the submission
environment.

For the repository's default trace directories, prebuilt driver, simulated
capacity of 158976 nodes, ten-minute polling, and automatic result collection,
the complete submit-and-monitor workflow is one command:

```bash
experimental/fugaku-power/scripts/submit_and_monitor_easypower_fiscal.sh
```

Use `--help` to see path, capacity, polling, and collection overrides.

After the allocation finishes, collect one submission into wide metric and long
comparison tables (plus a compact Markdown table):

```bash
experimental/fugaku-power/scripts/collect_easypower_fiscal_results.py \
  /path/to/results/run-JOB_ID
```

The files are placed in that run's `aggregate/` directory. The collector also
refuses to overwrite existing aggregate files; pass a new `--output-dir` when
you want another collected snapshot.

To monitor the submitted allocation every ten minutes, pass the numeric ID
printed by `sbatch` to the monitor:

```bash
experimental/fugaku-power/scripts/monitor_easypower_fiscal_job.sh JOB_ID \
  --output-root /path/to/results
```

Add `--collect` to create the aggregate tables automatically after a successful
completion. Use `--interval SECONDS` to override the default 600-second poll
interval.
