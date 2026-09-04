# Tutorial: Your First Simulation

This tutorial walks you through running your first DR_EVT simulation.

## Step 1: Prepare a Test Trace

Create a simple test trace with 3 jobs:

```bash
cat > my_first_trace.csv << EOF
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,100
10,15,0,pbatch,30
20,60,0,pbatch,50
EOF
```

**What this means:**
- Job 0: Arrives at t=0, needs 80 nodes, duration 100 seconds
- Job 1: Arrives at t=10, needs 15 nodes, duration 30 seconds
- Job 2: Arrives at t=20, needs 60 nodes, duration 50 seconds

## Step 2: Run the Simulation

```bash
./build/simulator my_first_trace.csv \
  --total_nodes 100 \
  --trace_format simple \
  --timestamp_format epoch \
  --duration_mode exact \
  --backfill_policy easy \
  --outfile results.csv
```

## Step 3: Understand the Output

The simulator will display:

```
=== Simulation Statistics ===
Total jobs: 3
Jobs submitted: 3
Jobs completed: 3
Current time: 150
Total nodes: 100
Average wait time: 26.67 sec
Average turnaround time: 86.67 sec
Makespan: 150 sec
```

## Step 4: Analyze Results

View the output file:

```bash
cat results.csv
```

Expected output:
```text
job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit
0,0,100,80,0,pbatch,100
10,10,40,15,0,pbatch,30
20,100,150,60,0,pbatch,50
```

**What happened:**
1. **t=0**: Job 0 starts (80 nodes)
2. **t=10**: Job 1 arrives and **backfills** (15 nodes fit in remaining 20)
3. **t=20**: Job 2 arrives but must wait (needs 60 nodes, only 5 free)
4. **t=40**: Job 1 completes
5. **t=100**: Job 0 completes, Job 2 can now start
6. **t=150**: Job 2 completes

## Step 5: Visualize (Optional)

Create a simple visualization:

```python
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv('results.csv')

fig, ax = plt.subplots(figsize=(10, 4))
for idx, row in df.iterrows():
    ax.barh(idx, row['end_time'] - row['begin_time'], 
            left=row['begin_time'], height=0.5,
            label=f"Job {idx} ({row['num_nodes']} nodes)")

ax.set_xlabel('Time (seconds)')
ax.set_ylabel('Job')
ax.set_title('Job Schedule Timeline')
ax.legend()
plt.savefig('timeline.png')
```

## Understanding Backfilling

In this example, Job 1 **backfilled**:
- Job 2 was waiting for Job 0 to complete (FCFS head)
- Job 1 arrived later but was small enough to fit
- Job 1 would complete (t=40) before Job 2's reservation (t=100)
- So Job 1 ran ahead of Job 2

This is **EASY backfilling** - it improves system utilization without delaying the waiting job.

## Common Variations

### Without Backfilling (Pure FCFS)

```bash
./build/simulator my_first_trace.csv \
  --backfill_policy none \
  --total_nodes 100
```

Result: Job 1 would wait until Job 0 completes, even though resources are available.

### Different Priority Policies

```bash
# Shortest Job First
./build/simulator my_first_trace.csv \
  --priority_policy sjf \
  --total_nodes 100

# Longest Job First
./build/simulator my_first_trace.csv \
  --priority_policy ljf \
  --total_nodes 100
```

## Next Steps

- [Command-Line Options](../user-guide/command-line.md) - All available options
- [User Guide](../user-guide/overview.md) - Complete guide with trace formats and scheduling policies
- [Backfilling Algorithms](../BACKFILLING_ALGORITHMS.md) - EASY and CONSERVATIVE specifications
- [Testing Guide](../TESTING_GUIDE.md) - How we verify correctness

## Exercises

Try these modifications:

1. **Add more jobs** - What happens with 10 jobs?
2. **Change resources** - Use `--total_nodes 50` - does Job 1 still backfill?
3. **Different durations** - Make Job 1 duration 100 instead of 30
4. **Real trace** - Try one of the test traces in `tests/test_traces/correctness/`
