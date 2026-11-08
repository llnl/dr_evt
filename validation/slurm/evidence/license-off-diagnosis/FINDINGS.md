# License-off divergence diagnosis

The endpoint-corrected DR_EVT comparison diverges from license-off Slurm for
three identified reasons: future pooled-capacity protection, main-dispatch
admission, and ordering within simultaneous event batches. This is different
from Fluxion's binding of protected reservations to specific nodes.

Ordinary Slurm node reservations protect both capacity over the planned
execution interval and the specific nodes supplying that capacity. DR_EVT
protects capacity counts without binding reservations to specific nodes.
The saved native run assigns each job a private physical node and represents
shared capacity with 756 global licenses. Physical reservations therefore
cannot protect the shared pool when future license planning is disabled.
Both license settings retain this resource mapping; switching `bf_licenses`
does not change node binding. License-on reserves future shared capacity,
matching DR_EVT. **License-off is an experimental control that removes
protection of earlier jobs' planned starts, not ordinary Slurm backfill on
shared compute nodes.**

## First main-dispatch difference

At 13 seconds, job 56 requests 512 licenses with only 504 available. DR_EVT
stops strict FCFS dispatch there. License-off Slurm skips job 56 and starts
jobs 57, 58, and 59 at 13 seconds. DR_EVT starts them at 30, 30, and 68 seconds.
This difference occurs before the first periodic backfill tick.

In `external/slurm/src/slurmctld/job_scheduler.c`, the
`ESLURM_LICENSES_UNAVAILABLE` branch stops scheduling only when `bf_licenses`
is enabled. With it disabled, scheduling continues to later jobs.
`external/slurm/src/plugins/sched/backfill/backfill.c` also skips jobs that
fail the current-license check and omits the future license map.

## First simultaneous-event difference

At 798 seconds, jobs 135 and 180 each release one license. Their releases are
the first difference between a batched, skipping-dispatch replay and native
Slurm. The native log shows dispatch between their individual completion
RPCs: job 243 consumes the first license, and job 245 consumes the second.
Job 232, earlier in the queue and requiring two licenses, waits until 847.
A replay that applies both releases before dispatch starts job 232 at 798.

The existing adapter gates backfill while applying an event batch, but main
dispatch can run between `COMPLETE` calls. Submission RPCs can also attempt
immediate allocation. DR_EVT applies all equal-time events before scheduling.
This ordering is a property of the saved adapter experiment; production
controller and RPC timing need not produce the same interleaving.

## Controlled full-trace reconstruction

The inclusive-search Python reference reproduces the corrected C++ DR_EVT
schedule exactly before any diagnostic change. Against the same audited
native license-off schedule:

| Replay | Differing starts | Area between curves (node-seconds) |
|---|---:|---:|
| Corrected DR_EVT baseline | 1,943 | 12,141,068 |
| Remove future pooled reservations only | 1,941 | 1,264,820 |
| Also consider later jobs when an earlier job cannot fit now; batch events | 1,603 | 939,964 |
| Skipping dispatch, no future pool protection, individual events | 0 | 0 |

The last replay reproduces all 2,000 native start and completion times.
Each replay checks arrivals, runtimes, and capacity. Changes have interacting
effects; the table does not assign additive percentages to individual causes.
The diagnostic reuses the native run rather than rerunning Slurm.

Scripts are `diagnose_license_off.py` and `plot_license_off_diagnosis.py` in
this directory. Results, complete replay schedules, input hashes, native log
excerpts, plotting data, and PDF/SVG/PNG figures are saved under:

```text
validation/slurm/results/licenses/controlled/license-off-diagnosis/
```

Reproduce in a fresh output directory:

```sh
python3 validation/slurm/licenses/diagnose_license_off.py \
  validation/slurm/results/licenses/controlled/endpoint-fixed/bf-licenses-off/lassen-prefix2000-hybrid-r32-q64/report.json \
  path/to/fresh/diagnosis \
  --reference validation/slurm/results/licenses/controlled/endpoint-rule-experiment/inclusive_searches/reference.py \
  --native-run validation/slurm/results/licenses/controlled/paired-license-off/lassen-prefix2000-hybrid-r32-q64
python3 validation/slurm/licenses/plot_license_off_diagnosis.py path/to/fresh/diagnosis
```

## Adapter control

`event_trace.py --batch-main-dispatch` enables a main-dispatch gate alongside
the existing backfill gate. It adds Slurm's `defer` option so submission RPCs
queue work, and dispatch runs after the equal-time RPC batch is released.
An arrival queues a job; it does not start it. DR_EVT makes known same-time
arrivals eligible and processes completions/releases before scheduling. The
gate matches this batching but does not force Slurm main dispatch to precede
backfill after release. Real Slurm can schedule between messages, so batching
is a simulation timing choice rather than a universal production ordering.
`build_event.sh` builds the controller hook; the gate is opt-in through
`SLURM_VALIDATION_BATCH_DISPATCH=1`. Native placement and reservation rules
are retained. This control includes a two-release regression with licenses
enabled and disabled. It changes validation timing, not DR_EVT scheduler code.
All eight native controls pass, including the two simultaneous-release cases.
The corrected adapter has not yet been rerun on the full Lassen trace; the
existing full-trace plots retain their original provenance.
