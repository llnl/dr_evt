# Output Trace Files

DR_EVT can write two CSV outputs for each run: a simulated-job schedule and a
resource-usage trace. Both use the timestamps selected by
`--timestamp_format`; see [Command-Line Options](command-line.md).

## Simulated-job schedule

Use `--outfile` to choose the simulated-job schedule file:

```bash
./build/simulator input.csv --outfile results/jobs_sim.csv
```

If `--outfile` is omitted, DR_EVT derives a filename from the input trace
(for example, `jobs.csv` becomes `jobs_sim.csv`). The CSV contains one row for
each scheduled job. In the default ID-input build, an input trace that
explicitly provides `q_id` retains that column:

```text
job_submit_time,begin_time,end_time,num_nodes,exit_status,q_id,time_limit
```

When the selected queue field is omitted from input, it is omitted from
output too:

```text
job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit
```

`begin_time` and `end_time` are the schedule produced by simulation, or the
historical values replayed in replay mode. `exit_status` is written as `0`.
Jobs rejected before scheduling are not written to this output. With
`-DDR_EVT_LEGACY_QUEUE_INPUT=ON`, the equivalent selected input/output column
is the legacy named `queue` field instead of `q_id`.

**TODO — user-defined queue names:** Preserve user-defined input queue names
instead of emitting only DR_EVT's built-in canonical names.

## Resource-usage trace

Use `--resource_trace` to name the resource-usage file explicitly:

```bash
./build/simulator input.csv \
    --outfile results/jobs_sim.csv \
    --resource_trace results/resources.csv
```

The resource trace records the occupancy after each resource-state change:

```text
time,free_nodes,allocated_nodes
0,100,0
0,20,80
10,5,95
40,20,80
100,100,0
```

Without `--resource_trace`, DR_EVT writes this output as
`<outfile>_resources.csv` (for example, `jobs_sim_resources.csv`).

## Memory behavior

The job-record store and resource-history buffer that support these outputs
are both circular buffers. Resource samples are flushed to the resource trace
when the configured `--resource_history_capacity` is full; see
[Command-Line Options](command-line.md) and the
[Circular Queue design notes](../dev/design-decisions/CIRCULAR_QUEUE.md).

## See also

- [Input Trace Files](trace-formats.md) - accepted input formats and columns
- [Command-Line Options](command-line.md) - `--outfile`, `--resource_trace`,
  and `--resource_history_capacity`
