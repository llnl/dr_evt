# DR_EVT User Guide

## Introduction

DR_EVT (Discrete Resource Event Modeling) is a high-performance HPC job scheduler simulator implementing SLURM-style backfilling algorithms. Use it to:
- Evaluate scheduling policies (EASY vs Conservative backfill)
- Test priority policies (FCFS, SJF, LJF)
- Compare run time estimation strategies
- Analyze HPC workload traces

## Quick Start

### Basic Usage

```bash
./simulator my_trace.csv \
  --total_nodes 100 \
  --backfill_policy easy \
  --priority_policy fcfs \
  --duration_mode actual
```

### Example Output

```
Loaded 30 jobs from trace
Running simulation with 100 nodes

Job 0 submitted at 0 (50 nodes)
Job 0 started at 0 (50 nodes)
...
Job 29 ended at 2010

=== Simulation Statistics ===
Total jobs: 30
Jobs completed: 30
Average wait time: 227 sec
Makespan: 2010 sec
```

## Trace File Formats

### Simple Format (Recommended for Testing)

7-column CSV format:
```text
job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit
0,0,100,10,0,pbatch,100
50,100,150,10,0,pbatch,50
```

**Columns**:
1. `job_submit_time` - When job arrives (required)
2. `begin_time` - Historical start time (required, for duration calculation)
3. `end_time` - Historical end time (required, for duration calculation)
4. `num_nodes` - Number of nodes requested (required)
5. `exit_status` - Job exit code (optional)
6. `queue` - Queue name, must be "pbatch" or "pall" (optional)
7. `time_limit` - User-provided time limit in seconds (optional)

**Usage**:
```bash
./simulator trace.csv \
  --trace_format simple \
  --timestamp_format epoch \
  --total_nodes 100
```

### Lassen Format (LLNL HPC Traces)

33-column format used by LLNL Lassen supercomputer:
- Full HPC trace format
- Includes user info, job scripts, etc.
- Backward compatible with existing traces

**Usage**:
```bash
./simulator lassen_trace.csv \
  --trace_format lassen \
  --timestamp_format iso \
  --timezone America/Los_Angeles \
  --total_nodes 795
```

## Timestamp Formats

### Epoch (Unix Time)

Integer seconds since 1970-01-01:
```text
0,0,100,10,0,pbatch,100      # Time 0, 100 seconds
50,100,150,10,0,pbatch,50    # Time 50, 100, 150
```

**Usage**: `--timestamp_format epoch`

**Advantages**:
- Simple integer format
- No timezone issues
- Fast parsing
- Best for synthetic test traces

### ISO (Human-Readable)

ISO 8601 format with timezone:
```text
2024-01-15T00:00:00,2024-01-15T00:00:00,2024-01-15T00:01:40,10,0,pbatch,100
```

**Usage**: 
```bash
--timestamp_format iso --timezone America/Los_Angeles
```

**Advantages**:
- Human-readable
- Used by real HPC traces
- Timezone-aware

**Supported timezones**: Any POSIX timezone (UTC, America/New_York, etc.)

## Scheduler Policies

### Backfill Policies

#### EASY Backfill (Default)

**Algorithm**:
1. First job in queue gets guaranteed start time (reservation)
2. Later jobs can "backfill" if they:
   - Fit in available resources NOW
   - Won't delay first job's reservation

**Characteristics**:
- Simple, fast
- Good resource utilization
- Favors first job in queue
- Used by most HPC centers

**Usage**: `--backfill_policy easy`

**Example**:
```
Job 0 (80 nodes, 1000s) - First in queue, gets reservation
Job 1 (10 nodes, 100s) - Arrives later, backfills immediately
Job 2 (15 nodes, 100s) - Can't fit, waits for Job 0
```

#### Conservative Backfill

**Algorithm**:
1. ALL queued jobs get guaranteed start times
2. Backfilling jobs cannot delay ANY reservation

**Characteristics**:
- More complex scheduling
- Guarantees to all jobs
- Lower risk of starvation
- May have lower utilization

**Usage**: `--backfill_policy conservative`

**When to use**: Research comparing policies, fairness studies

### Priority Policies

#### FCFS (First-Come-First-Served) - Default

Jobs processed in submission order.

**Usage**: `--priority_policy fcfs`

**Characteristics**:
- Simple, fair
- No starvation
- May waste resources with large jobs

#### SJF (Shortest-Job-First)

Prioritize jobs with shortest run time.

**Usage**: `--priority_policy sjf`

**Characteristics**:
- Maximizes throughput
- Minimizes average wait time
- Can starve long jobs
- Requires run time estimates

#### LJF (Longest-Job-First)

Prioritize jobs with longest run time.

**Usage**: `--priority_policy ljf`

**Characteristics**:
- Useful for special scenarios
- Can increase average wait time
- Research/comparison purposes

### Scheduler's Run Time Estimate (Duration Mode)

#### USE_LIMIT (Realistic Mode) - Default

Scheduler uses user-provided time limits for planning.

**Usage**: `--duration_mode limit`

**Characteristics**:
- Realistic (users provide limits)
- Limits often overestimate
- More resource waste
- Use for realistic simulations

#### USE_ACTUAL (Omniscient/Oracle Mode)

Scheduler plans using the job's real, observed run time (from trace).

**Usage**: `--duration_mode actual`

**Characteristics**:
- Perfect information
- Best possible scheduling
- Unrealistic but useful for upper bound
- Use for comparison baseline

**Note**: Requires `time_limit` column in trace

## Command-Line Options

### Required Options

```bash
--total_nodes N          # Total system nodes (required)
```

### Input/Output Options

```bash
-i, --infile FILE        # Input trace file (required)
-o, --outfile FILE       # Output file (default: based on input name)
-j, --max_jobs N         # Max jobs to simulate (default: all)
```

### Trace Format Options

```bash
-f, --trace_format FORMAT     # Trace format: simple|lassen (default: lassen)
-T, --timestamp_format FORMAT # Timestamp: epoch|iso (default: iso)
-z, --timezone ZONE           # Timezone for ISO timestamps (default: America/Los_Angeles)
```

### Scheduler Options

```bash
-b, --backfill_policy POLICY   # Backfill: easy|conservative|none (default: easy)
-p, --priority_policy POLICY   # Priority: fcfs|sjf|ljf (default: fcfs)
-r, --duration_mode MODE        # Scheduler's planning estimate: limit|actual (default: limit)
-q, --queue_impl IMPL          # FCFS wait queue: circular|deque|multimap|block (default: circular)
-A, --circular_capacity SIZE   # Initial capacity for queue_impl=circular (default: 0 = size of trace)
-G, --circular_overflow POLICY # abort|grow if circular capacity exceeded (default: grow)
```

### Other Options

```bash
-h, --help              # Show help message
-s, --seed N            # Random seed (default: 0)
```

## Usage Examples

### Example 1: Basic Test

Test with synthetic trace (realistic mode):
```bash
./simulator test_traces/epoch_pbatch.csv \
  --trace_format simple \
  --timestamp_format epoch \
  --total_nodes 100 \
  --backfill_policy easy \
  --priority_policy fcfs \
  --duration_mode limit
```

### Example 2: Real HPC Trace

Simulate Lassen trace:
```bash
./simulator lassen_trace.csv \
  --trace_format lassen \
  --timestamp_format iso \
  --timezone America/Los_Angeles \
  --total_nodes 795 \
  --backfill_policy easy \
  --priority_policy fcfs \
  --duration_mode limit
```

### Example 3: Policy Comparison

Compare EASY vs Conservative:
```bash
# EASY backfill
./simulator trace.csv --backfill_policy easy -o results_easy.txt

# Conservative backfill  
./simulator trace.csv --backfill_policy conservative -o results_conservative.txt

# Compare results
diff results_easy.txt results_conservative.txt
```

### Example 4: Priority Policy Study

Compare FCFS vs SJF:
```bash
# FCFS
./simulator trace.csv --priority_policy fcfs -o results_fcfs.txt

# SJF
./simulator trace.csv --priority_policy sjf -o results_sjf.txt
```

### Example 5: Run Time Estimation Impact

Compare oracle vs realistic:
```bash
# Oracle (perfect information)
./simulator trace.csv --duration_mode actual -o results_oracle.txt

# Realistic (user time limits)
./simulator trace.csv --duration_mode limit -o results_realistic.txt
```

## Understanding Output

### Job Events

```
Job 0 submitted at 0 (50 nodes)
  Resources allocated: 50 nodes, 50/100 remaining
Job 0 started at 0 (50 nodes)
Job 0 ended at 500
  Resources freed: 50 nodes, now 100/100 free
```

**Interpretation**:
- Job arrives at time 0
- Needs 50 nodes
- Starts immediately (resources available)
- System has 50/100 free after allocation
- Job completes at time 500
- All resources returned

### Backfill Messages

```
Job 1 submitted at 10 (15 nodes)
  Backfill failed: job 1 needs 15 nodes, only 10 available
```

**Interpretation**: Job can't backfill due to insufficient resources

### Statistics

```
=== Simulation Statistics ===
Total jobs: 30
Jobs submitted: 30
Jobs completed: 30
Current time: 2010
Total nodes: 100
Average wait time: 227 sec
Average turnaround time: 500 sec
Makespan: 2010 sec
```

**Metrics**:
- **Wait time**: Time from submission to start
- **Turnaround time**: Time from submission to completion
- **Makespan**: Time from first submit to last completion

## Common Workflows

### 1. Create Test Trace

```bash
cat > my_test.csv << EOF
job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit
0,0,100,50,0,pbatch,100
10,10,60,10,0,pbatch,100
20,20,60,5,0,pbatch,100
EOF
```

### 2. Run Simulation

```bash
./simulator my_test.csv \
  --trace_format simple \
  --timestamp_format epoch \
  --total_nodes 100 \
  --backfill_policy easy
```

### 3. Analyze Results

Check output for:
- All jobs completed
- Resource utilization
- Wait times
- Backfill opportunities

### 4. Compare Policies

```bash
# Run with different policies
for policy in easy conservative; do
  ./simulator my_test.csv \
    --backfill_policy $policy \
    -o results_$policy.txt
done

# Compare
diff results_easy.txt results_conservative.txt
```

## Troubleshooting

### Problem: "Loaded 0 jobs from trace"

**Cause**: Queue filtering - only "pbatch" and "pall" queues accepted

**Solution**: 
- Use "pbatch" in queue column
- Or set `SHOW_ALL_QUEUE=1` in common.hpp and rebuild

### Problem: "Job event times are incorrect"

**Cause**: Submit time > begin time in trace

**Solution**: Ensure in trace: `submit_time ≤ begin_time ≤ end_time`

### Problem: "Resource over-subscription"

**Cause**: Bug in scheduler (should not happen with current code)

**Solution**: 
- Check total_nodes matches system
- Verify job node counts
- Report bug with trace file

### Problem: Very long simulation time

**Cause**: Large trace or infinite loop

**Solution**:
- Use `--max_jobs` to limit
- Check for scheduler deadlock
- Monitor with `top` command

## Performance Tips

### For Large Traces

1. **Limit job count during testing**:
   ```bash
   ./simulator large_trace.csv --max_jobs 100
   ```

2. **Use simple trace format**:
   - Faster parsing than Lassen format
   - Convert large traces to simple format first

3. **Disable verbose output**:
   - Comment out debug prints in scheduler.cpp
   - Rebuild for production

### Expected Performance

- **Small traces** (< 100 jobs): < 10ms
- **Medium traces** (100-1000 jobs): 10-100ms
- **Large traces** (1000-10000 jobs): 100ms-1s
- **Very large** (10000+ jobs): 1-10s

## Advanced Topics

### Creating Custom Traces

```python
import pandas as pd

# Generate synthetic trace
jobs = []
for i in range(100):
    submit_time = i * 10
    duration = random.randint(50, 500)
    nodes = random.choice([5, 10, 20, 50])
    
    jobs.append({
        'job_submit_time': submit_time,
        'begin_time': submit_time,  # Will be rescheduled
        'end_time': submit_time + duration,
        'num_nodes': nodes,
        'exit_status': 0,
        'queue': 'pbatch',
        'time_limit': duration + 100
    })

df = pd.DataFrame(jobs)
df.to_csv('synthetic_trace.csv', index=False)
```

### Batch Processing

```bash
#!/bin/bash
# Compare all policies on multiple traces

for trace in traces/*.csv; do
  for policy in easy conservative; do
    for priority in fcfs sjf ljf; do
      output="results/$(basename $trace .csv)_${policy}_${priority}.txt"
      ./simulator $trace \
        --backfill_policy $policy \
        --priority_policy $priority \
        -o $output
    done
  done
done
```

### Extracting Metrics

```bash
# Extract average wait times from multiple runs
grep "Average wait time" results/*.txt | \
  awk '{print $5}' | \
  awk '{sum+=$1; n++} END {print "Mean:", sum/n}'
```

## Best Practices

1. **Start small**: Test with 10-100 jobs before large traces
2. **Validate traces**: Check job counts, resource bounds
3. **Use version control**: Track traces and results
4. **Document experiments**: Note policy combinations tested
5. **Compare baselines**: Always compare against FCFS+EASY
6. **Check completion**: Verify jobs_submitted == jobs_completed

## References

- [Testing Guide](../TESTING_GUIDE.md) - Running test suite and test philosophy
- [CLI Options](command-line.md) - Complete command-line reference
- [Backfilling Algorithms](../BACKFILLING_ALGORITHMS.md) - EASY and CONSERVATIVE specifications
- [Streaming API](../STREAMING_API.md) - Online simulation API

## Support

For issues or questions:
1. Check troubleshooting section
2. Review test cases in `test_traces/`
3. Check documentation files
4. File issue with trace file and command used
