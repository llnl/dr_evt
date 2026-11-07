# Node-reservation placement discrepancy

## Four-job reproducer

The system has **4 nodes total**, with one core per node. Both cases use FCFS,
event-driven dispatch with DR_EVT `--per_pass_accounting`, Q=2048, no horizon,
and no simulated release delay.
Time limits equal actual durations. The matcher is **`hinodex` (high-ID-first)**
in both cases; only the input order of the two jobs arriving at time 0 changes.

| Job | Arrival | Nodes | Duration |
|---|---|---|---|
| A | 0 | 2 | 70 |
| B | 0 | 2 | 80 |
| C, waiting head | 6 | 3 | 10 |
| D, backfill candidate | 6 | 1 | 50 |

### Case 1: original order diverges

Submit A before B at time 0, then C before D at time 6.

| Job | Fiction nodes | Fiction execution | DR_EVT execution |
|---|---|---|---|
| A | 2,3 | 0–70 | 0–70 |
| B | 0,1 | 0–80 | 0–80 |
| C | 1,2,3 | 80–90 | 80–90 |
| D | 0 | 80–130 | 70–120 |

At 70, A releases nodes **2,3**. Both belong to C's reservation for 80–90,
so D cannot use either for its 50-second run. Node 0, outside that reservation,
is still occupied by B until 80. Fiction starts D then. DR_EVT protects three
nodes by count and admits D at 70; it does not emit particular node IDs.

### Case 2: swapped order matches

Submit B before A at time 0, keeping C before D at time 6. High-ID-first now
puts B on nodes 2,3 and A on nodes 0,1.

| Job | Fiction nodes | Fiction execution | DR_EVT execution |
|---|---|---|---|
| A | 0,1 | 0–70 | 0–70 |
| B | 2,3 | 0–80 | 0–80 |
| C | 1,2,3 | 80–90 | 80–90 |
| D | 0 | 70–120 | 70–120 |

At 70, A releases nodes **0,1**. Node **0 is outside C's reservation**, so D
can start there immediately without delaying C. All start and finish times
match. D starts exactly at A's completion time: no simulated node-release or
RPC delay is added in this case. RPC execution still takes wall-clock time.

Separate hybrid R=32 captures confirm C's reservation on **1,2,3** in both
cases at time 6. In the diverging capture, A's allocation is absent at time 70,
while C still reserves 1,2,3. In the matching capture, D is allocated node 0 at
70. Runs with and without these diagnostic queries give identical schedules.
These captures are in `results/high-id-four-job-assignments/` and
`results/high-id-swapped-four-jobs/`.

### Run and verification

[`reproduce_divergence.py`](reproduce_divergence.py) runs both cases under EASY,
hybrid R=32, and conservative. It checks Fiction's actual node assignments,
the expected start times, and agreement or disagreement of complete schedules.
Optional single-job deletions from the diverging case provide twelve three-job
controls, all of which match. Four is the smallest verified job count from this
reduction, rather than a claim about all possible workloads.

In the prepared container:

```sh
python3 /validation/reproduce_divergence.py "$(cat /workspace/dr_evt-simulator)" \
  --check-deletions
```

The six paired cases, twelve deletion controls, input files, commands,
transitions, results, and source snapshot are recorded under
`results/minimal-divergence/`. Each report entry includes job labels and actual
Fiction node assignments. These cases do not reach Q and do not test truncated
passes. A successful check of the diverging case means the documented difference
was reproduced; it does not mean that the two schedules agree.
