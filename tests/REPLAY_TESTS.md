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

The replay test runner (`run_replay_tests.sh`) uses several correctness tests as inputs:
- 01_backfill_allowed
- 05_multiple_backfills
- 13_consecutive_fcfs

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
| Input | `job_submit_time`, `time_limit` | `num_nodes`, `begin_time`, `end_time`, `job_submit_time`, `queue`, `time_limit` - all required (`job_submit_time` isn't used for scheduling here, but it drives the per-job "nodes busy at submission" stat, and there's no shorter format that omits it) |
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

1. Choose a correctness test as input (small, verified correct)
2. Add to `REPLAY_TESTS` array in `run_replay_tests.sh`
3. Run: `./tests/run_replay_tests.sh`

## Current Status

✓ Replay test infrastructure created
- 3-step test methodology implemented
- Uses verified correctness tests as input
- Compares resource traces automatically

Ready for:
- Running replay tests
- Adding more test cases

Last updated: 2026-08-28
