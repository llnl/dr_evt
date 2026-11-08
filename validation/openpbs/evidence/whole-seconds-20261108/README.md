# Recorded native OpenPBS comparisons

OpenPBS 23.06.06, revision `cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a`,
was compared with DR_EVT using private vnodes and shared server capacity.
The build verified all 63 native scheduler `.cpp`/`.h` files against the
committed source. No native scheduling patches were applied.

| Comparison | Jobs | Differing schedules | Result |
|---|---:|---:|---|
| Seven focused controls plus twelve randomized cases | 384 total | 0 | Exact matches |
| Four-job adjacent-release case | 4 | 2 | Expected difference from skipping the check between releases |
| Same four-job case with times doubled | 4 | 0 | Exact match |
| Converted Lassen prefix, R=32, capacity=756 | 2,000 | 1,433 | Measured divergence |
| Same Lassen prefix with all times doubled | 2,000 | 0 | Exact match |

Every comparison passed capacity bounds, job duration, resource-area, and
private-vnode ownership checks. Job identifiers are one-based positions in
stable arrival order, not original workload-manager identifiers.

The primary Lassen replay has mean waits of 23,620.5925 seconds for OpenPBS
and 23,287.413 seconds for DR_EVT. Utilization is 75.0899% and 74.9136%,
respectively. Detailed metrics and both complete schedules are in
[summary.json](summary.json) and the individual reports.

## The rules needed to interpret the difference

In these comparisons, jobs are considered in arrival order. A later job may
start ahead of a waiting job only if it will not delay that waiting job's
predicted start. This is backfill. The prediction uses requested time limits
to forecast when running jobs release capacity, and it protects capacity for
the waiting job's requested runtime. Capacity released at a time is available
for another job starting at the same time.

The current scheduling decision and its future prediction happen at different
levels. The adapter processes actual arrivals and completions at their exact
timestamps, then calls the scheduler. Inside that call, the scheduler examines
a hypothetical future to predict a waiting job's start. Actual replay time
remains fixed while it calculates that prediction.

With the tested `opt_backfill_fuzzy=off`, OpenPBS first checks the current
simulated state. If the waiting job cannot run, it takes the earliest remaining
predicted event at time T and groups all events through **T + 1 second**,
including that endpoint. It applies all events in the group before checking
whether the waiting job fits. After a group releases capacity, that check uses
the time of the **last event actually processed**. If the job still cannot run,
the forecast continues with the next group. Completions at 10 and 11 are
therefore processed together and checked at 11; completions at 10 and 12 are
separate, with the first check at 10. A job finishing at 100 can still be
running at either check. There is no automatic one-second delay and no grouping
of the outer loop's actual triggers.

The source is `simulate_events()` in OpenPBS's `src/scheduler/simulate.cpp`,
lines 188–219 at the recorded revision. `calc_run_time()`, lines 749–773,
checks whether the waiting job fits after that group returns. The setting is
documented as a speed/precision tradeoff. Although `off` is represented by
zero, the implementation replaces zero with one before calculating its group
endpoint, which explains the remaining one-second grouping.

## Four jobs that reproduce the difference

There are three shared capacity slots. All jobs arrive at zero, in order
A, B, H, S. Requested limits equal actual runtimes:

| Job | Slots needed | Runtime and limit in seconds | OpenPBS start | DR_EVT start |
|---|---:|---:|---:|---:|
| A | 1 | 10 | 0 | 0 |
| B | 1 | 11 | 0 | 0 |
| H | 2 | 2 | 11 | 10 |
| S | 1 | 11 | 0 | 11 |

A and B start at zero, leaving one of the three slots free. H needs two slots
and must wait. The current decision, still at zero, is whether S may use the
spare slot without delaying H.

If S waits, A's completion at 10 increases free slots from one to two.
B's completion at 11 increases free slots to three. DR_EVT checks at 10,
finds enough capacity for H, and protects that start. S would occupy a slot
until 11 if started now, leaving only one slot for H at 10, so DR_EVT rejects
S at zero. Its actual starts are H at 10 and S at 11.

OpenPBS groups A's predicted completion at 10 with B's at 11. It applies both
and then checks H once, with three free slots at 11. It does not check H when
two slots first become available at 10. It therefore protects H's start at 11.
S can finish at 11 without delaying that prediction, so OpenPBS allows S at
zero. Its actual loop still handles completions at 10 and 11 separately, but
S has already started and keeps H from running at 10.

## The first different decision in Lassen

At time 9,731 seconds, both schedulers have the same running jobs, waiting
queue, and one free capacity unit. OpenPBS starts job 548; DR_EVT starts job
740. During that current decision, the forecast examines jobs 528 and 536
finishing at 13,006 and job 546 finishing at 13,007. These are hypothetical
future completions, not actual events being handled at time 9,731.

DR_EVT checks after the releases at 13,006 and predicts job 229 can start then.
OpenPBS groups those releases with the release at 13,007, checks afterward,
and predicts job 229's start at 13,007. Starting job 548 now would leave too
little capacity for job 229 at 13,006, but enough at 13,007. The independent
capacity calculation confirms why OpenPBS permits job 548 and DR_EVT rejects
it. See [first-divergence.json](lassen-prefix2000-r32/first-divergence.json).

## Repeating the input with every time multiplied by two

“Times doubled” means a second replay with every arrival time, actual runtime,
and requested time limit multiplied by two. Job order and resource requests
are unchanged. For example, A's duration becomes 20 seconds and B's becomes
22. Their predicted releases are now two seconds apart, so OpenPBS checks them
separately. Both schedulers start H at 20 and S at 22. Applying the same change
to the full 2,000-job input makes every start and completion match. This
supports the identified cause; the original replay still differs.

The [primary figure](lassen-prefix2000-r32/comparison.svg) and
[spacing-control figure](lassen-prefix2000-r32-spacing2/comparison.svg) show
capacity, cumulative starts, and per-job waits. PDF versions are beside them.
`workload.json` contains the converted input, not the original fractional-time
trace. `trace-conversion.json` records its selection and raw-source hash.

The native event file beside the primary report is an excerpt containing the
first-divergence batch. The full native event streams and logs remain in the
ignored results directory. `provenance.json`, build manifests, configuration,
commands, and source snapshots identify the runs. Recorded primary and spacing
comparison scripts are retained alongside the current reproduction scripts.

The final build-and-test check passed five audit tests, all 21 small-case
expected outcomes, and the ordinary/doubled 50-job trace cases. The recorded
full replays contain the 2,000-job results above. Run the complete comparison
with `bash validation/openpbs/run.sh`; see the
[harness README](../../README.md) for dependencies and scope.
