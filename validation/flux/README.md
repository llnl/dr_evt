# Native scheduling validation with Fiction

This container harness compares DR_EVT policy sets with native Flux schedulers
driven by Fiction. The [published validation page](../../docs/user-guide/FLUX_VALIDATION.md)
presents the identical FCFS run, the hybrid-backfill comparison, and the
four-job start-time divergence with Fiction. The FCFS run uses `sched-simple`,
with Fluxion unloaded. The hybrid-backfill run and four-job reproducer use
Fluxion to make scheduling and resource-placement decisions; they do not use
`sched-simple`. Fiction drives simulated arrivals and completions in both
experiments. DR_EVT implements its own capacity-count scheduler.
Every DR_EVT validation invocation uses `--per_pass_accounting`.

## Build and run the checks

Install Git and Docker on the host. The image provides Linux dependencies;
Flux Core, Fluxion, and Fiction are built from local source checkouts, not
downloaded by the image. From a fresh repository checkout, obtain the tested
revisions:

```sh
mkdir -p external
git clone https://github.com/flux-framework/flux-core.git external/flux-core
git -C external/flux-core checkout --detach d0309fcf0e672d90e65b0797168e56a430b9fb57
git clone https://github.com/flux-framework/flux-sched.git external/flux-sched
git -C external/flux-sched checkout --detach 9eba598b4e0940d597695e65f20f5357fc513427
git clone https://github.com/flux-framework/flux-fiction.git external/flux-fiction
git -C external/flux-fiction checkout --detach 346cbd65bf7da3798e2a351b44e0f57a5b20da96
```

`flux-sched` supplies Fluxion; `flux-core` also supplies sched-simple.
No host Flux installation or running cluster is required. Run:

```sh
bash validation/flux/run.sh
```

The runner builds `dr-evt-flux-validation` from the local Dockerfile, exports
the committed native sources into its build volume, builds and installs them
there, and runs the checks. To build only the dependency image:

```sh
docker build -t dr-evt-flux-validation validation/flux
```

The default sources are committed checkouts in
`external/{flux-core,flux-sched,flux-fiction}`. `FLUX_SOURCES` overrides that
location. Source exports are built in a Docker volume; mounted checkouts are
read-only. `FLUX_VALIDATION_BUILD_VOLUME` selects the reusable volume; use a
fresh volume after changing native source revisions or synchronization overlays.
DR_EVT is exported from its current commit and built with Release optimization
and `DR_EVT_LEGACY_RESERVATION_BARRIER=OFF`.

The default run checks synchronization, Fiction smoke cases, paired
policies, sched-simple's thirty small
comparisons, and both minimal-reproducer matchers. Backend revisions, source
hashes, commands, and logs are retained under `results/`.
`FLUX_VALIDATION_RESULTS` overrides the host results directory.

## Recorded Lassen comparisons

Prepare the local extraction using the exact submission-time interval in the
[parent page's workload description](../../docs/user-guide/SCHEDULER_VALIDATION.md#lassen-workload),
and save it as `lassen_traces/lassen-756-01.csv`. Mount `lassen_traces`
read-only at `/lassen_traces` in the prepared container.
Run these commands inside that container:

```sh
python3 /validation/validate_lassen.py /lassen_traces/lassen-756-01.csv   "$(cat /workspace/dr_evt-simulator)" --policy none   --scheduler-backend sched-simple --queue-depth 64 --max-jobs 2000   --timeout 900 --outdir /results/sched-simple-lassen-prefix2000

python3 /validation/validate_lassen.py /lassen_traces/lassen-756-01.csv   "$(cat /workspace/dr_evt-simulator)" --policy hybrid   --scheduler-backend fluxion --reservation-depth 32 --queue-depth 64   --max-jobs 2000 --timeout 900 --outdir /results/lassen-hybrid-prefix2000
```

The runner accepts extracted logs with `job_submit_time,begin_time,end_time`
and logs with `submit_time,duration`. The commands above select the
2,000-job workload described on the parent page.

`sched_simple.py` inherits the installed native Python sched-simple's
allocation, scheduling, and forecast methods unchanged. It adds read-only
synchronization and resource snapshots. `sched_simple_support.py` adapts
Fiction's loader and records native hashes and loaded modules. Fluxion is
unloaded for that run. Its Q bounds outstanding requests, not per-pass tests.
The two backends are selected explicitly; backfill is rejected for sched-simple.

These are different validation models. Sched-simple uses native FIFO scheduling
without backfill or future node reservations. Fluxion retains hybrid backfill
and binds reservations to particular nodes, while DR_EVT reserves interchangeable
capacity counts. DR_EVT does not model Fluxion's resource graph, node matching,
or node-specific reservations. An observed FCFS match does not validate
Fluxion backfill or ordinary production placement. Equal priorities, one unrestricted queue,
whole-node exclusive jobs, and an initially empty cluster are experiment
conditions rather than general claims about Flux deployments.

An existing scheduler simulator is not an appropriate reference for an exact
comparison with DR_EVT's abstract resource model unless it models
interchangeable capacity or provides a way to configure that abstraction.
Specific-node reservations introduce placement effects absent from DR_EVT.
Sched-simple supplies the no-backfill control here; it cannot validate
backfill. The separate native Slurm experiment uses private dummy nodes and
shared license capacity, with `bf_licenses=on` protecting future counts, to
retain backfill while removing competition for specific nodes. Matching the
resource model is necessary but does not establish matching scheduler order
or policy.

## Minimal reproducer and focused checks

```sh
python3 /validation/test_sched_simple.py "$(cat /workspace/dr_evt-simulator)"
python3 /validation/reproduce_divergence.py "$(cat /workspace/dr_evt-simulator)"   --match-policy lonodex --check-deletions --outdir /results/minimal-divergence-low
python3 /validation/reproduce_divergence.py "$(cat /workspace/dr_evt-simulator)"   --match-policy hinodex --check-deletions --outdir /results/minimal-divergence-high
```

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

Fiction advances its emulated time after that handshake; host RPC elapsed time
is not added to the simulated job schedule. `account_system_latency=false`,
`quiescent_accumulation_window=0.0`, and `batch_job_starts=true` record the
timing conditions. Fiction emulates execution and completion using the supplied
actual runtimes; no resource-release delay is configured. The Fluxion hook and
sched-simple wrapper observe scheduling state without replacing native
allocation or reservation decisions. These controls are specific to Fiction;
they do not use the Slurm adapter's `TIME` counter or dispatch-acknowledgement
mechanism. Native revisions, synchronization patches, configurations and hashes
identify the tested setup. A match applies to that recorded workload and setup.

Published figures, plotting data, and compact evidence are checked into
`docs/_static/scheduler-validation/` and `evidence/`. Raw logs, diagnostic runs,
and generated results remain ignored. The plotting script is standalone:

```sh
python3 plot_comparison.py --plot-input plot-input.json
```
