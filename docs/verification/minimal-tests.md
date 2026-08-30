# Minimal Correctness Test Suite for EASY Backfilling

## Core Properties to Verify

EASY backfilling must satisfy these invariants:

1. **FCFS Head Priority**: First job in queue gets reservation (guaranteed start time)
2. **Backfilling Constraint**: Backfilled jobs complete BEFORE FCFS head's reservation
3. **Resource Constraint**: Never over-subscribe (allocated ≤ total available)
4. **Time Ordering**: Jobs never start before their submit_time
5. **Duration Correctness**: Jobs run for specified duration
6. **Completeness**: All submitted jobs eventually complete

## Minimal Test Cases

### 1. Single Job (Trivial Case)
**Purpose:** Verify basic job execution
- 1 job arrives at t=0
- Sufficient resources available
- **Expected:** Job starts immediately at t=0

**Status:** ✓ Can use `correctness/easy_5jobs_input.csv` (just Job 0)

### 2. Concurrent Jobs (Resource Check)
**Purpose:** Verify multiple jobs can run simultaneously when resources permit
- 2 jobs arrive at t=0
- Combined resources ≤ total available
- **Expected:** Both start at t=0

**Status:** ✓ Have `correctness/basic_2jobs_input.csv`

### 3. Sequential Waiting (FCFS)
**Purpose:** Verify FCFS ordering when resources insufficient
- Job 0: arrives t=0, needs 80 nodes
- Job 1: arrives t=0, needs 60 nodes
- Total: 100 nodes
- **Expected:** Job 0 starts t=0, Job 1 waits until t=Job0_end

**Status:** ⚠️ MISSING - Need to create

### 4. Basic Backfilling (Core Feature)
**Purpose:** Verify small job can backfill while large job runs
- Job 0: t=0, 80 nodes, 100s (FCFS head, runs immediately)
- Job 1: t=10, 60 nodes, 50s (must wait - would need 140 total)
- Job 2: t=20, 15 nodes, 20s (can backfill! finishes before Job 0 ends)
- **Expected:** Job 0 at t=0, Job 2 at t=20 (backfills), Job 1 at t=100

**Status:** ✓ Have `correctness/backfill_3jobs_input.csv`

### 5. Backfill Blocked (Delays FCFS Head)
**Purpose:** Verify backfilling doesn't delay FCFS head reservation
- Job 0: t=0, 80 nodes, 50s
- Job 1: t=10, 60 nodes, 50s (FCFS head after Job 0)
- Job 2: t=20, 15 nodes, 60s (would finish at t=80, AFTER Job 1's reservation at t=50)
- **Expected:** Job 2 must wait, cannot backfill (would delay Job 1)

**Status:** ⚠️ MISSING - Need to create

### 6. Job at t=0 (Our Bug Case)
**Purpose:** Verify jobs arriving at initial time are handled correctly
- Job 0: t=0, 80 nodes, 100s
- Job 1: t=10, 15 nodes, 20s
- **Expected:** Job 0 starts at t=0 (NOT t=10!)

**Status:** ✓ Have `correctness/easy_5jobs_input.csv` - This is our primary test!

### 7. Staggered Arrivals
**Purpose:** Verify time ordering and dynamic scheduling
- Jobs arrive at different times (t=0, 10, 20, 30, 40)
- Mix of sizes and durations
- **Expected:** Each job scheduled correctly when it arrives

**Status:** ✓ Have `correctness/easy_5jobs_input.csv`

### 8. Resource Saturation
**Purpose:** Verify no over-subscription when resources fully utilized
- Multiple jobs competing for limited resources
- Total demand >> total supply
- **Expected:** Never allocate > 100 nodes at any time

**Status:** ✓ Covered by `correctness/easy_5jobs_input.csv` (our original over-subscription bug test)

### 9. Idle Gap Detection
**Purpose:** Verify scheduler finds gaps between running jobs
- Job 0: t=0, 50 nodes, 30s
- Job 1: t=0, 50 nodes, 30s (both run, fills system)
- Job 2: t=10, 80 nodes, 20s (must wait until t=30)
- Job 3: t=20, 40 nodes, 15s (can start at t=30, gap available)
- **Expected:** Job 3 starts at t=30 (not delayed by Job 2's reservation)

**Status:** ✓ Have `correctness/idle_gap_input.csv`

## Minimal Required Test Set

**Essential (Must Have):**
1. ✓ easy_5jobs - Comprehensive test covering multiple scenarios
2. ✓ basic_2jobs - Concurrent execution
3. ✓ backfill_3jobs - Basic backfilling
4. ⚠️ **MISSING: sequential_wait** - FCFS waiting (no backfill)
5. ⚠️ **MISSING: backfill_blocked** - Backfill constraint violation

**Recommended (Should Have):**
6. ✓ idle_gap - Gap detection between jobs

**Current Status:**
- ✓ Have: 4/6 essential tests
- ⚠️ Missing: 2 tests

## Missing Tests to Create

### Test: sequential_wait.csv
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,60,0,pbatch,40
```

**Expected Reference Output:**
```csv
job_idx,submit_time,start_time,end_time,nodes,duration
0,0.0,0.0,50.0,80,50.0
1,0.0,50.0,90.0,60,40.0
```

**Verifies:** FCFS ordering when no backfilling possible

### Test: backfill_blocked.csv
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
10,60,0,pbatch,50
20,15,0,pbatch,60
```

**Expected Reference Output:**
```csv
job_idx,submit_time,start_time,end_time,nodes,duration
0,0.0,0.0,50.0,80,50.0
1,10.0,50.0,100.0,60,50.0
2,20.0,100.0,160.0,15,60.0
```

**Reasoning:**
- Job 0: starts t=0
- Job 1: FCFS head after Job 0, reservation at t=50
- Job 2: arrives t=20, needs 15 nodes (available: 20 nodes)
  - If Job 2 starts at t=20, ends at t=80
  - But Job 1's reservation is t=50
  - Job 2 would delay Job 1 → BLOCKED from backfilling
  - Must wait until t=100

**Verifies:** Backfilling constraint (cannot delay FCFS head)

## Test Coverage Matrix

| Property | Test Case | Status |
|----------|-----------|--------|
| FCFS Priority | easy_5jobs, basic_2jobs | ✓ |
| Backfilling | backfill_3jobs | ✓ |
| Backfill Constraint | **backfill_blocked** | ⚠️ MISSING |
| Resource Constraint | easy_5jobs | ✓ |
| Time Ordering | easy_5jobs | ✓ |
| Duration | All tests | ✓ |
| Completeness | All tests | ✓ |
| FCFS Waiting | **sequential_wait** | ⚠️ MISSING |
| t=0 Handling | easy_5jobs | ✓ |
| Idle Gap | idle_gap | ✓ |

## Implementation Plan

1. **Create missing tests:**
   ```bash
   # Create test inputs
   echo "job_submit_time,num_nodes,exit_status,queue,time_limit
   0,80,0,pbatch,50
   0,60,0,pbatch,40" > tests/test_traces/correctness/sequential_wait_input.csv
   
   echo "job_submit_time,num_nodes,exit_status,queue,time_limit
   0,80,0,pbatch,50
   10,60,0,pbatch,50
   20,15,0,pbatch,60" > tests/test_traces/correctness/backfill_blocked_input.csv
   ```

2. **Generate reference outputs:**
   ```bash
   python3 scripts/python_reference_scheduler.py \
     tests/test_traces/correctness/sequential_wait_input.csv \
     --nodes 100 --output sequential_wait_reference.csv
   
   python3 scripts/python_reference_scheduler.py \
     tests/test_traces/correctness/backfill_blocked_input.csv \
     --nodes 100 --output backfill_blocked_reference.csv
   ```

3. **Verify DR_EVT matches:**
   ```bash
   ./build/simulator tests/test_traces/correctness/sequential_wait_input.csv \
     --total_nodes 100 --trace_format simple --timestamp_format epoch \
     --duration_mode exact --outfile output.csv
   
   python3 scripts/compare_with_oracle.py \
     tests/test_traces/correctness/sequential_wait_reference.csv output.csv
   ```

## Acceptance Criteria

All 6 essential tests must pass:
- ✓ easy_5jobs
- ✓ basic_2jobs  
- ✓ backfill_3jobs
- ⚠️ sequential_wait
- ⚠️ backfill_blocked
- ✓ idle_gap

**Definition of Pass:** DR_EVT output exactly matches reference implementation for all jobs (start_time, end_time).

## Beyond Minimal Set

Once minimal set passes, add:
- Conservative backfilling tests
- Priority policy tests (SJF, LJF)
- Multi-queue tests
- Edge cases (empty trace, single node system, etc.)
