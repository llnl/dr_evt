# Native scheduling validation with Fiction

This container harness compares DR_EVT policy sets with native Flux schedulers
driven by Fiction. The [published validation page](../../docs/user-guide/SCHEDULER_VALIDATION.md)
presents the identical FCFS run using sched-simple, the hybrid-backfill
comparison using Fluxion, and the four-job start-time divergence.
Every DR_EVT validation invocation uses `--per_pass_accounting`.

## Build and run the checks

From the repository root:

```sh
bash validation/flux/run.sh
```

The default sources are committed checkouts in
`external/{flux-core,flux-sched,flux-fiction}`. `FLUX_SOURCES` overrides that
location. Source exports are built in a Docker volume; mounted checkouts are
read-only. `FLUX_VALIDATION_BUILD_VOLUME` selects the reusable volume; use a
fresh volume after changing native source revisions or synchronization overlays.
DR_EVT is exported from its current commit and built with Release optimization
and `DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`.

The default run checks synchronization, Fiction smoke cases, paired small
policies, two 2,000-job synthetic FCFS traces, sched-simple's thirty small
comparisons, and both minimal-reproducer matchers. Backend revisions, source
hashes, commands, and logs are retained under `results/`.
`FLUX_VALIDATION_RESULTS` overrides the host results directory.

## Recorded Lassen comparisons

Mount the user-provided `lassen_traces` folder read-only at `/lassen_traces` in
the prepared container. The trace is not checked into this repository.
Run these commands inside that container:

```sh
python3 /validation/validate_lassen.py /lassen_traces/lassen-756-01.csv   "$(cat /workspace/dr_evt-simulator)" --policy none   --scheduler-backend sched-simple --queue-depth 64 --max-jobs 2000   --timeout 900 --outdir /results/sched-simple-lassen-prefix2000

python3 /validation/validate_lassen.py /lassen_traces/lassen-756-01.csv   "$(cat /workspace/dr_evt-simulator)" --policy hybrid   --scheduler-backend fluxion --reservation-depth 32 --queue-depth 64   --max-jobs 2000 --timeout 900 --outdir /results/lassen-hybrid-prefix2000
```

The runner selects the earliest jobs in stable arrival order. It preserves
arrivals, limits, and durations on an initially empty pool of 756 whole-node
units. It accepts subset logs with `job_submit_time,begin_time,end_time` and
whole logs with `submit_time,duration`. `--max-jobs 0` selects the whole file.
The published results cover 2,000 jobs, not the full source log.

`sched_simple.py` inherits the installed native Python sched-simple's
allocation, scheduling, and forecast methods unchanged. It adds read-only
synchronization and resource snapshots. `sched_simple_support.py` adapts
Fiction's loader and records native hashes and loaded modules. Fluxion is
unloaded for that run. Its Q bounds outstanding requests, not per-pass tests.
The two backends are selected explicitly; backfill is rejected for sched-simple.

## Synthetic workloads and minimal reproducer

```sh
python3 /validation/test_paired.py "$(cat /workspace/dr_evt-simulator)"   --suite large --policy none --queue-depth 64
python3 /validation/test_paired.py "$(cat /workspace/dr_evt-simulator)"   --suite large --policy hybrid --queue-depth 64 --reference-timeout 900
python3 /validation/test_sched_simple.py "$(cat /workspace/dr_evt-simulator)"
python3 /validation/reproduce_divergence.py "$(cat /workspace/dr_evt-simulator)"   --match-policy lonodex --check-deletions --outdir /results/minimal-divergence-low
python3 /validation/reproduce_divergence.py "$(cat /workspace/dr_evt-simulator)"   --match-policy hinodex --check-deletions --outdir /results/minimal-divergence-high
```

The synthetic traces have 2,000 jobs each on four nodes, in bursts of 24 or 48
spaced 4,000 simulation seconds apart. They use 1–4 nodes and 10–80-second
limits equal to actual durations. FCFS matches; hybrid R=32/Q=64 has small
resource-curve differences. `test_paired.py` exits nonzero for a schedule
mismatch; this is an expected outcome for the published hybrid comparisons,
not a claim of exact backfill agreement. Timeouts remain incomplete runs.

The [minimal reproducer](DIVERGENCE.md) verifies the identified node-reservation
placement difference with four jobs. Expected divergences and matching controls
are assertions, so its checks exit successfully when the observations agree.

## Resource evidence and synchronization

`resource_observer/sitecustomize.py` is opt-in through
`DR_EVT_OBSERVE_RESOURCES=1`. Before Fiction advances time, it reads native
allocated nodes and each running job's assignment. `resource_profiles.py`
checks capacity, duplicate owners, stale allocations, and agreement with job
records. DR_EVT's curve comes from `--resource_trace` at resource-accounting
changes. Areas integrate the step curves exactly.

```sh
python3 /validation/resource_profiles.py "$(cat /workspace/dr_evt-simulator)"   --paired-report /results/paired/report.json --paired-only
python3 -m unittest test_resource_profiles
```

The snapshots and compared schedules come from the same captured run. Snapshot
RPCs take wall-clock time; they do not add a configured simulation latency.
Equal total resource areas alone cannot detect two jobs claiming the same node,
which is why ownership is checked separately.

The handshake waits for submissions/completions to reach Flux, no active or
requested scheduler pass, allocation responses, and resulting starts.
It does not issue extra scheduling passes. Tests use unheld jobs in one
unrestricted queue. Reservation-only reset investigations are separate from
these completed comparisons and are not published here.

Published figures, plotting data, and compact evidence are checked into
`docs/_static/scheduler-validation/` and `evidence/`. Raw logs, diagnostic runs,
and generated results remain ignored. The plotting script is standalone:

```sh
python3 plot_comparison.py --plot-input plot-input.json
```
