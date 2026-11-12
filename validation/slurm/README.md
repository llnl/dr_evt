# Periodic backfill validation with native Slurm

The default comparison uses native Slurm from `external/slurm` with pooled
license capacity and our trace-driven validation adapter.

## Download sources and prepare the container

Install Git and Docker on the host. From a fresh DR_EVT checkout, obtain
native Slurm at the revision used by the published comparison:

```sh
mkdir -p external
git clone https://github.com/SchedMD/slurm.git external/slurm
git -C external/slurm checkout --detach 17abb848e20054009d136a2c504a23e5462f26c8
```

The default event campaign now also writes `results/placement/`. That run
removes the synthetic license pool, submits each job as an exclusive N-node
request on a shared homogeneous pool, and compares Slurm's native node list
with the exact IDs selected by DR_EVT placement scheduling. The historical
protected and no-reservation capacity runs remain separate. Native and DR_EVT
process times in all three trees are collected on the same campaign host.

The image contains Linux build tools and dependencies, not a downloaded Slurm
checkout. The runner mounts `external/slurm` read-only and exports its committed
source into a Docker build volume, where it builds Slurm and DR_EVT. It adds
the recorded clock/gating hooks to that exported copy. The host checkout is
unchanged. No host Slurm installation, database, or compute-node daemons are
needed.

Prepare the local extraction using the exact submission-time interval in the
[parent page's workload description](../../docs/user-guide/SCHEDULER_VALIDATION.md#lassen-workload),
and save it as `lassen_traces/lassen-756-01.csv` in the repository root. Run:

```sh
bash validation/slurm/run.sh
```

This builds `dr-evt-slurm-validation` from the local Dockerfile and starts the
native validation harness. To build the dependency image separately:

```sh
docker build -t dr-evt-slurm-validation validation/slurm
```

`SLURM_LICENSE_BUILD_VOLUME` selects the reusable build volume (default
`dr-evt-slurm-license-build`). Use a fresh volume name after changing Slurm
revisions or configure options; an existing volume retains exported sources.
Slurm's version, source revision, adapter hooks, and binary hashes are saved
with each run. Historical EPFL simulator tools are separate from this command.

It runs the small controls and the chronological 2,000-job Lassen prefix
with `bf_licenses` enabled and disabled. Lassen results are saved in `paired/`
and `paired-license-off/`, respectively.
Results, copied scripts, plotting inputs, resource curves, and PDF/SVG/PNG
figures are saved under `results/licenses/`. Set `SLURM_LICENSE_RESULTS` to
a fresh output directory to choose its location.
The result root also contains `license-planning-metrics.csv`,
`license-planning-comparison.json`, and `summarize_licenses.py` for the
matched on/off comparison. The runner enables equal-time dispatch gating and
selects protected DR_EVT for license-on, and
`--diagnostic_no_reservations` for license-off. It also saves the protected
DR_EVT schedule as a separate license-off control.

## Resource model and scheduler scope

An existing scheduler simulator is not an appropriate reference for an exact
comparison with DR_EVT's abstract resource model unless it models
interchangeable capacity or provides a mechanism to configure that abstraction.
Simulating specific-node reservations can introduce placement differences
that DR_EVT does not represent. Here, private dummy nodes and shared licenses
provide the abstraction, and `bf_licenses=on` protects future shared capacity.
`bf_licenses` alone does not remove node binding. This resource-model condition
is necessary for direct comparison; matching scheduling order and policy is
also required, and an observed match does not prove general equivalence.

The diagnostic scans all eligible jobs in FCFS order and starts those that
fit current capacity, without reserving future capacity. It is a compile-time
specialization of the periodic hybrid scheduler; existing loops gain no
per-job mode check. R/Q/H and planning resolution do not constrain its scan.
The container snapshots tracked working files so uncommitted diagnostic
changes are included; the binary and source hashes record the exact build.

Ordinary Slurm backfill protects the FCFS head job's planned start through
resource capacity and node binding: enough capacity must be available over
the planned interval, and a reservation identifies the specific nodes that
supply it. Reserving those nodes protects both their capacity and identities.
This experiment removes competition for those nodes: each job has one
private dummy node and requests its original node
count as shared `capacity` licenses, from a pool of 756. A private-node
reservation cannot protect the shared license capacity.

Both license-on and license-off runs use this setup; `bf_licenses` does not
change node binding. With it on, Slurm reserves future license counts,
matching DR_EVT's protection of planned starts. With it off, only current
license limits apply, so later jobs can delay earlier jobs. **License-on is
the intended abstract-resource comparison. License-off is an experimental
control, not ordinary Slurm node backfill behavior.**
Slurm defaults to `bf_licenses` off because ordinary node reservations already
protect compute capacity; the option adds future planning for separate
license resources. We use licenses as node capacity, so enabling it is closer
to ordinary Slurm's protection of planned starts in this setup.
Ordinary Slurm's specific-node reservations can nevertheless cause the same
kind of divergence from DR_EVT shown in the Fiction four-job example.
The recorded license-on match demonstrates agreement for that run with
abstract capacity. Main-versus-backfill order is not enforced, so it does
not establish agreement for every order Slurm can use or ordinary node placement.
Backfill remains enabled at 30-second intervals; dispatch throttling is zero.
DR_EVT selects `FCFSPeriodicHybridCircularScheduler`, implemented in
`src/sim/scheduler_fcfs_periodic_hybrid_circular.cpp`, with FCFS ordering,
`--queue_impl circular --backfill_policy hybrid --dispatch_interval 0
--backfill_interval 30`. State events dispatch FCFS jobs; timer ticks backfill.
The adapter supplies virtual time and native registration, allocation,
completion, and epilog RPCs. A recorded synchronization hook serializes event
batches with native backfill; allocation and reservation algorithms remain
native Slurm. The lightweight observer fetches full job records on state
transitions; pending-job forecast timestamps may be stale in snapshots.

## How virtual nodes are configured

A virtual node is a named compute-node record in Slurm's controller. In this
harness, the record is backed by a daemon on the test host or by the validation
adapter. Each job requests its own private node, so jobs never compete for
that node's identity. Shared `capacity` licenses represent the original trace's
node demand. The following two setups use this resource model; the default
trace-driven comparison uses the adapter setup.

### Earlier wall-clock controls: multiple slurmd daemons on one host

[licenses/compare.py](licenses/compare.py) runs a native `slurmctld` and one
`slurmd` per private node. Slurm is built with `--enable-multiple-slurmd`, as
shown in [licenses/build.sh](licenses/build.sh). All node addresses point to
localhost, but each daemon has a distinct port, spool directory, and PID file.
An excerpt of the generated four-node configuration is:

```ini
SlurmdSpoolDir=/tmp/slurm-control/spool-%n
SlurmdPidFile=/tmp/slurm-control/slurmd-%n.pid
SlurmdParameters=config_overrides
Licenses=capacity:4
NodeName=case0n0 NodeHostname=localhost NodeAddr=127.0.0.1 Port=18000 CPUs=1 RealMemory=128 State=UNKNOWN
NodeName=case0n1 NodeHostname=localhost NodeAddr=127.0.0.1 Port=18001 CPUs=1 RealMemory=128 State=UNKNOWN
```

The harness creates each spool directory and starts the daemons with their
logical node names. For those two nodes, the commands are equivalent to:

```sh
slurmd -D -f /tmp/slurm-control/slurm.conf -N case0n0
slurmd -D -f /tmp/slurm-control/slurm.conf -N case0n1
```

These are separate processes launched by the harness; `-D` keeps each in the
foreground. It waits for the nodes to register as idle, then submits jobs
with `--nodes=1 --nodelist=case0n0 --licenses=capacity:2`, for example. That
request represents a two-node trace job on one private dummy node. Jobs in
this setup execute short `sleep` scripts, and elapsed wall time drives their
completion. The configuration above is an excerpt, not a complete standalone
`slurm.conf`.

### Default trace-driven comparison: registration and completion RPCs

[licenses/event_trace.py](licenses/event_trace.py) runs native `slurmctld`
without launching `slurmd` processes. It defines one node per input job:

```ini
SlurmdTimeout=0
Licenses=capacity:756
NodeName=n[0000-1999] NodeHostname=localhost NodeAddr=127.0.0.1 Port=18000 CPUs=1 RealMemory=128 State=IDLE
```

This excerpt illustrates the 2,000-job, 756-node Lassen comparison. The harness
generates the node range and license count from the workload. After the
controller's startup probe, it sends `REGISTER n0000`, and the corresponding
command for every other node, to [licenses/client.c](licenses/client.c).
The client sends `MESSAGE_NODE_REGISTRATION_STATUS`, the registration RPC
normally sent by `slurmd`, with the node name and configured CPU/memory values.
`SlurmdTimeout=0` disables subsequent daemon-liveness probes.

For submission, the client calls `slurm_allocate_resources()` with the job's
private node as `req_nodes`, one requested compute node, and
`capacity:<original node demand>` as its licenses. A 32-node trace job thus
uses one private node and 32 of the 756 shared licenses. Slurm's native
selection and backfill plugins decide when that request can start.

No job process executes in this setup. The adapter observes Slurm's allocation,
advances virtual time to the observed start plus the recorded actual duration,
and sends the job-completion and epilog-completion RPCs to release resources.
The adapter supplies node
registration and workload events; Slurm retains ownership of allocation and
reservation decisions. Virtual nodes and the virtual clock are separate parts
of the harness; the clock and event batching are described below.

## Event batching, acknowledgements, and clock handling

Main scheduling tries to start waiting jobs when jobs arrive or finish.
Periodic backfill looks for additional jobs that can run. DR_EVT runs main
scheduling first when both are due. Native Slurm can run them in either order;
backfill's bounded examination depth makes this order observable.

`licenses/event_trace.py --batch-main-dispatch` supplies all simultaneous
completion and arrival RPCs while both scheduler gates are held. An arrival
puts a job in the waiting queue; it does not start it. The adapter enables
Slurm's `defer` option to prevent allocation inside a submission RPC. It then
releases both gates together. **This controls event batching, but does not
control main-versus-backfill order.** Ordinary Slurm can also schedule between
separate messages; batching is an explicit experiment condition.

After each event batch, the driver waits for the completed-main-pass counter
(`DISPATCH`) to advance and for native observations to settle before advancing
virtual time. Unchanged snapshots alone do not prove that scheduling ran.

The virtual clock uses whole seconds for job events and a subsecond counter
for `gettimeofday()`. Repeating `TIME` with the same second preserves that
counter; resetting it could move Slurm's observed clock backward and suppress
initial dispatch, even with throttling disabled. Monotonic and RPC timeout
clocks remain real, and host elapsed time does not advance job-event time.
Both license settings use this clock fix and dispatch acknowledgement.

Jobs are not executed by compute daemons. At the observed start plus the
recorded actual duration, the adapter sends native completion and epilog RPCs.
All same-time completions are applied before the gates open. Resource-release
delay is zero; the inclusive endpoint fit rule does not extend job runtimes.

An earlier gated license-on run exposed backfill-before-main ordering and two
30-second start delays. A separate first license-off attempt missed initial
dispatch, causing 972 differing starts. Controlled replays explain those
recorded runs in [LICENSE_OFF_FINDINGS.md](licenses/LICENSE_OFF_FINDINGS.md).
They are historical diagnoses, not the current comparison. An exact match in
a later run does not establish
agreement for every order Slurm can use or bound possible divergence.

## Reproducing current results and independent audits

The validation run produces small-case figures with feasible assignments to
node-equivalent rows for license planning on and off, protected DR_EVT, and
diagnostic DR_EVT. It also produces a six-panel Lassen figure.
Both native runs use the corrected clock and dispatch acknowledgements, the
same freshly built Slurm and adapter binaries, and the same current simulator
binary. Any exact matches are outcomes under uncontrolled scheduler order.

The protected comparison uses reservation depth 32, backfill examination depth 64,
planning resolution one second, and a 72-hour planning horizon. The Lassen
prefix uses the [common Lassen workload](../../docs/user-guide/SCHEDULER_VALIDATION.md#lassen-workload).
Recorded runtimes are
rounded up to whole seconds in both inputs; arrivals and whole-minute time
limits are preserved. `workload.json` records the input hash and selected rows.

The adapter records native job states and license usage at event and timer
boundaries. `report.json` compares starts, waiting times, utilization, makespan,
and resource curves. `audit.json` independently verifies arrivals, runtimes,
license counts, private node ownership, and both schedulers' resource traces.
A passing audit does not imply matching schedules.

To reproduce the comparison and its audits from source, use a fresh result
directory:

```sh
SLURM_LICENSE_RESULTS="$PWD/validation/slurm/results/licenses/reproduction" \
  bash validation/slurm/run.sh
```

This rebuilds and runs native Slurm and DR_EVT, then saves schedules, resource
curves, audits, source and binary hashes, plotting data, and figures in that
directory. Use a different empty directory for each reproduction.

Historical 2,000-job results before the endpoint correction are in
`results/licenses/controlled/dr-evt-performance-on-20261108/`:
`lassen-prefix2000-hybrid-r32-q64/` compares with license planning enabled,
and `bf-licenses-off/lassen-prefix2000-hybrid-r32-q64/` compares with it
disabled. Both reuse audited native Slurm schedules and rerun DR_EVT.
The enabled case includes five DR_EVT process timings in `performance.json`.
At that stage, the schedules still had 1,266 differing starts with planning on and
1,943 with it off. Both four-job scenarios were rerun under `controls/`.

To measure DR_EVT separately after producing a fresh comparison:

```sh
python3 validation/slurm/licenses/measure_dr_evt.py path/to/result --runs 5
```

The timer includes process launch, input, simulation, output and exit;
it excludes Python schedule parsing, auditing and plotting. Every timed
schedule and resource trace is checked against the comparison run.

The first enabled divergence before the endpoint correction was job 605
at an exact reservation endpoint. Reconstruct and plot its evidence with:

```sh
python3 validation/slurm/licenses/diagnose_reservation_boundary.py path/to/result path/to/diagnostics
python3 validation/slurm/licenses/plot_reservation_boundary.py path/to/diagnostics/first-divergence-data.json
```

## Historical endpoint and skipped-tick diagnoses

The earlier skipped-tick figure is retained under
`divergence/skipped-tick-before-fix/`. That boundary plot isolates one case; the controlled endpoint reruns below
account for the full enabled trace.

The historical endpoint-fix comparison is in
`results/licenses/controlled/endpoint-fixed/`. With license planning enabled,
all 2,000 DR_EVT start/completion times and resource curves matched the recorded
native Slurm run exactly; this did not control scheduler order. The disabled comparison remains a different reservation policy.
Both four-job scenarios were rerun and retain their previous schedules.

`results/licenses/controlled/endpoint-rule-experiment/` stores the independent
reference experiment: the pre-fix reference reproduces DR_EVT exactly;
correcting admission alone leaves 806 differing starts; correcting both
admission and reservation placement leaves none. The C++ rerun independently
confirms that result. Reproduce from the saved half-open reference with:

```sh
python3 validation/slurm/licenses/compare_endpoint_rules.py path/to/saved/report.json path/to/fresh/output --reference path/to/saved/baseline/reference.py
python3 validation/slurm/licenses/plot_endpoint_comparison.py path/to/fresh/output/endpoint-comparison.json
```

The old job-605 diagnostic remains historical evidence. The endpoint-corrected
periodic hybrid/conservative scheduler retains event-driven FCFS dispatch;
it changes both fit searches while preserving actual runtimes and half-open
capacity reservations. Other scheduling modes retain their existing fit rule.

To regenerate the six-panel plot or audit a saved result:

```sh
python3 path/to/result/plot.py path/to/result/plot-input.json
python3 validation/slurm/licenses/audit_event.py path/to/result
```

Historical EPFL simulator results remain in `results/controlled/` and
`evidence/`. Their setup is preserved in [EPFL_REFERENCE.md](EPFL_REFERENCE.md)
and `run_epfl.sh`; the default runner uses native Slurm.

To archive a completed gated run before publishing its figures, use the
validation image (which includes matplotlib):

```sh
python3 validation/slurm/licenses/publish_gated_results.py path/to/results \
  --evidence validation/slurm/evidence/gated-lassen \
  --docs docs/_static/scheduler-validation/slurm \
  --local-performance path/to/local/timing-results
```

The archive retains native snapshots, audits, schedules, resource traces,
plot inputs and scripts, configuration, binary/source hashes, and the exact
working-source patch. Original results remain historical evidence.

Both gated license settings require a completed main-dispatch
acknowledgement after each arrival/completion batch. Repeated `TIME` commands
at the same second preserve the subsecond counter, preventing backwards
`gettimeofday()` values at startup. Unchanged snapshots alone are insufficient:
the first gated attempt missed both time-zero starts, producing 972 different
starts. Delaying only those two jobs to one second in a controlled replay
reproduces that entire attempt. Reconstruct that diagnosis with:

```sh
python3 validation/slurm/licenses/diagnose_startup_dispatch.py \
  path/to/first-gated-license-off/report.json path/to/startup-diagnosis
```

Archived snapshots store job-record changes between frames (`job-deltas-v1`).
The auditor reconstructs every complete native snapshot and repeats the
original capacity/runtime checks; original stream hashes accompany the archive.
This preserves the observations without committing gigabytes of repeated rows.
