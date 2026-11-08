# Four-job start-time divergence

On a system with **4 nodes total**, Fluxion's specific-node reservation delays
backfill job D until 80 seconds. DR_EVT's node-count plan starts D at 70 while
preserving head job C's start at 80. The
[published validation page](../../docs/user-guide/SCHEDULER_VALIDATION.md#minimal-reproducer-for-backfill-start-time-differences)
includes the node-assignment diagram.

Both runs use FCFS, event-driven dispatch, DR_EVT `--per_pass_accounting`,
Q=2048, unlimited horizon, and zero simulated release delay. Every node has
one core, and durations equal submitted time limits. The workload never
reaches Q; depth-triggered pass resets are a separate issue.

| Job | Arrival | Nodes | Duration |
|---|---:|---:|---:|
| A | 0 | 2 | 70 |
| B | 0 | 2 | 80 |
| C, waiting head | 6 | 3 | 10 |
| D, backfill candidate | 6 | 1 | 50 |

## Low-ID-first case

Submit A before B at time 0, and C before D at time 6. `lonodex` selects low
node IDs first:

| Job | Fiction nodes | Fiction execution | DR_EVT execution |
|---|---|---|---|
| A | 0,1 | 0–70 | 0–70 |
| B | 2,3 | 0–80 | 0–80 |
| C | 0,1,2 | 80–90 | 80–90 |
| D | 3 | 80–130 | 70–120 |

At 70, A releases nodes 0,1. Both are reserved for C's 80–90 window, so D's
50-second run cannot use either. The only node outside C's reservation is
node 3, still occupied until 80. Fluxion starts D then.

DR_EVT protects three-node capacity rather than node IDs. One feasible
assignment gives D node 0 at 70–120 and C nodes 1,2,3 at 80–90. This keeps
running jobs on their nodes and preserves C's start. DR_EVT does not emit
this assignment; the diagram illustrates why its count-based decision fits.

Swapping A and B's equal-time input order places B on 0,1 and A on 2,3.
Now node 3 is released at 70 and lies outside C's reservation. D starts at
70 in both engines, and their complete schedules match.

## High-ID-first controls

The same two input orders are also checked with `hinodex`:

| Input order | A's nodes | B's nodes | C's nodes | Fiction D start | DR_EVT D start |
|---|---|---|---|---:|---:|
| A,B,C,D | 2,3 | 0,1 | 1,2,3 | 80 | 70 |
| B,A,C,D | 0,1 | 2,3 | 1,2,3 | 70 | 70 |

Each matcher is verified under EASY, hybrid R=32, and conservative. There are
three expected divergences, three matching swaps, and twelve matching
single-job deletion controls per matcher: **36 checks total**. Four jobs is
the smallest verified workload from this reduction, not an exhaustive minimum
across every possible configuration.

In the matching controls, D starts exactly at A's completion at 70. This
isolates a reservation-placement explanation for this example. It does not
explain every Lassen difference or rule out RPC ordering effects elsewhere.
Future reservations bind particular nodes; running allocations remain fixed.
Changing the global legacy reservation barrier is not needed for this case.

## Reproduce

In the prepared validation container:

```sh
python3 /validation/reproduce_divergence.py "$(cat /workspace/dr_evt-simulator)"   --match-policy lonodex --check-deletions --outdir /results/minimal-divergence-low
python3 /validation/reproduce_divergence.py "$(cat /workspace/dr_evt-simulator)"   --match-policy hinodex --check-deletions --outdir /results/minimal-divergence-high
```

The script checks actual Fiction node assignments, complete schedules, and
expected differences under each backfill policy. Each run retains inputs,
commands, configurations, transitions, and the simulator binary hash.
Checked reports are in [evidence](evidence/); generated runs remain under
ignored `results/`.
