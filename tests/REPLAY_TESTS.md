# Replay Mode Tests

## Overview

Replay mode tests verify that replaying a simulation produces identical resource usage. This ensures that the replay mechanism faithfully reproduces the original execution.

## Test Methodology

### 3-Step Process

1. **Run Simulation Mode**
   - Input: Trace with only `job_submit_time`
   - Output: Job trace (with `begin_time`/`end_time`) + Resource trace

2. **Replay Job Trace**
   - Input: Job trace from step 1 (has `begin_time`/`end_time`/`job_submit_time` - all three are required columns, see "Replay vs Simulation" below)
   - Tool: `tracer` (a separate binary from `simulator` - no scheduler linked in at all)
   - Output: Resource trace from replay

3. **Compare Resource Traces**
   - Must match exactly
   - Any difference indicates a bug in `tracer`'s resource accounting

### What This Tests

- **Resource accounting**: `tracer` derives occupancy identically to `simulator`, from the same begin/end times
- **Event ordering**: Same event order as the original run
- **Determinism**: Replay is deterministic given the same input
- **Safe job-store reclamation**: completed records are written exactly once
  and reclaimed only after their replay events and earlier records are safe

### What This Doesn't Test

`simulator` fed a 6-column (`begin_time`/`end_time` present) file does **not** bypass its scheduler - it still calls into the same FCFS/backfill logic as any other run, and overwrites the file's `begin_time` with whatever the scheduler decides (see `Trace::insert_job()`). Only `tracer` honors the recorded `begin_time`/`end_time` directly. So this test suite only exercises `tracer`, never `simulator` in a "replay" role.

## Running Tests

```bash
# Run all replay tests
./tests/run_replay_tests.sh

# Manual replay test:
# Step 1: Simulate
./build/simulator input.csv \
    --total_nodes 100 \
    --trace_format simple \
    --outfile sim_jobs.csv \
    --resource_trace sim_resources.csv

# Step 2: Replay - tracer, not simulator; no scheduler-related flags exist for it
./build/tracer --infile sim_jobs.csv \
    --total_nodes 100 \
    --resource_trace replay_resources.csv \
    --outfile tracer_out.csv \
    --subfile tracer_sub.csv \
    --subsumf tracer_subsum.csv

# Step 3: Compare
diff sim_resources.csv replay_resources.csv
# Should be identical!
```

## Test Cases

Before the CLI comparisons, the replay runner executes
`test_replay_reclamation`. That binary exercises:

- explicit partial and repeated flushes;
- periodic flushing without reclaiming events that have not been enqueued;
- the default capacity-sized flush interval;
- final output when no earlier flush occurred;
- multiple departures at the same timestamp;
- out-of-order completion and contiguous-front-prefix blocking; and
- exactly-once schedule output across reclamation and final output.

The runner then uses four scheduler-correctness fixtures as CLI inputs:

- 01_backfill_allowed
- 05_multiple_backfills
- 13_consecutive_fcfs
- 21_sustained_high_load

These are small, verified-correct simulations that make good replay test cases.

## Resource Trace Format

Resource traces show resource usage over time:

```csv
time,free_nodes,allocated_nodes
0,100,0
0,30,70
20,10,90
```

(Note the header order - `free_nodes` before `allocated_nodes` - matches what `simulator --resource_trace` and `tracer --resource_trace` actually write.) Replay must produce **byte-for-byte identical** resource traces.

## Why Replay Mode Exists

The `tracer` binary is useful for:
1. **Debugging**: Reproduce exact execution from logs, without a scheduler making its own decisions
2. **Validation**: Verify resource accounting is correct
3. **Visualization**: Replay execution for visualization tools
4. **Testing**: Test the resource-accounting engine independent of any scheduling policy

## Replay vs Simulation

| Feature | Simulation Mode (`simulator`) | Replay (`tracer`) |
|---------|----------------|-------------|
| Binary | `simulator` | `tracer` - a separate binary, no scheduler code linked in |
| Input | `job_submit_time`, `time_limit` | `num_nodes`, `begin_time`, `end_time`, `job_submit_time`, `time_limit` - all required; optional `q_id` defaults to `1` (`Queue1`). Legacy builds use optional `queue` instead. (`job_submit_time` isn't used for scheduling here, but it drives the per-job "nodes busy at submission" stat.) |
| Scheduler | Active - computes each job's start time | Not linked in - `begin_time`/`end_time` are taken directly from the file |
| Resources | Computed from the scheduler's decisions | Computed by replaying the file's own begin/end times |
| Purpose | Simulate scheduling policies | Reproduce a known execution's resource usage |

## Common Issues

### Resource Traces Differ

If resource traces don't match:
- **Event ordering bug**: Replay processes events differently
- **Resource accounting bug**: Resources allocated/freed incorrectly
- **State bug**: Replay and simulation have different state

### Replay Crashes

If replay crashes but simulation works:
- **Input validation**: Replay might have stricter validation
- **Invalid times**: `begin_time`/`end_time` might be inconsistent

## Adding Replay Tests

To add a new replay test:

1. Add a reclamation boundary to `test_replay_reclamation.cpp`, or choose a
   small, verified correctness fixture for a new CLI comparison.
2. For a CLI comparison, add its basename to the `REPLAY_TESTS` array in
   `run_replay_tests.sh`.
3. Build/install the test binaries and run `./tests/run_replay_tests.sh`.

## Current Status

✓ Five runner-level replay tests passing
- One reclamation-boundary binary
- Four three-step simulation/replay resource comparisons
- Resource traces compared automatically

Last updated: 2026-09-12
