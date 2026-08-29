# Replay Mode Tests

## Overview

Replay mode tests verify that replaying a simulation produces identical resource usage. This ensures that the replay mechanism faithfully reproduces the original execution.

## Test Methodology

### 3-Step Process

1. **Run Simulation Mode**
   - Input: Trace with only `job_submit_time` 
   - Output: Job trace (with `begin_time`/`end_time`) + Resource trace

2. **Replay Job Trace**
   - Input: Job trace from step 1 (has `begin_time`/`end_time`)
   - Output: Resource trace from replay

3. **Compare Resource Traces**
   - Must match exactly
   - Any difference indicates replay bug

### What This Tests

- **Resource accounting**: Replay uses resources identically to simulation
- **Event ordering**: Replay processes events in same order
- **State management**: Replay maintains same state as simulation
- **Determinism**: Replay is deterministic given same input

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

# Step 2: Replay
./build/simulator sim_jobs.csv \
    --total_nodes 100 \
    --trace_format simple \
    --resource_trace replay_resources.csv

# Step 3: Compare
diff sim_resources.csv replay_resources.csv
# Should be identical!
```

## Test Cases

The replay test runner uses several correctness tests as inputs:
- bf01_basic_success_input
- bf04_multiple_backfill_input
- easy_5jobs_input

These are small, verified-correct simulations that make good replay test cases.

## Resource Trace Format

Resource traces show resource usage over time:

```csv
timestamp,allocated_nodes,idle_nodes,total_nodes
0,80,20,100
10,95,5,100
60,80,20,100
```

Replay must produce **byte-for-byte identical** resource traces.

## Why Replay Mode Exists

Replay mode is useful for:
1. **Debugging**: Reproduce exact execution from logs
2. **Validation**: Verify resource accounting is correct
3. **Visualization**: Replay execution for visualization tools
4. **Testing**: Test simulator without scheduler (uses pre-recorded times)

## Replay vs Simulation

| Feature | Simulation Mode | Replay Mode |
|---------|----------------|-------------|
| Input | `job_submit_time` only | `begin_time`, `end_time` required |
| Scheduler | Active (computes times) | Inactive (uses provided times) |
| Resources | Computed from schedule | Computed from replay |
| Purpose | Simulate scheduling | Reproduce execution |

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
