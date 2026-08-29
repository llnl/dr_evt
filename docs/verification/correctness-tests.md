# Backfill Correctness Test Suite

## Test Progression

1. **Hand-traceable tests** - Verify basic logic by inspection
2. **Invariant checking** - Mathematical properties that must hold
3. **Contradiction tests** - Scenarios that should be impossible
4. **Self-consistency** - Deterministic and logically ordered
5. **Backfill correctness tests** ← THIS DOCUMENT
6. **Cross-validation** - DR_EVT vs Python oracle on all tests
7. **Large-scale validation** - 2000+ job trace comparison

## Purpose of Backfill Tests

After verifying basic correctness (1-4), systematically test ALL backfilling scenarios:

- Backfill succeeds (various conditions)
- Backfill blocked (various reasons)
- Multiple backfills
- Edge cases

## Backfill Test Suite

### BF-1: Basic Backfill Success
**Scenario:** Small job backfills while large job runs

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,100
10,15,0,pbatch,30
```

**Expected:**
- Job 0: starts t=0, ends t=100
- Job 1: starts t=10, ends t=40 (BACKFILLS)

**Verifies:** Basic backfilling works when constraint satisfied

---

### BF-2: Backfill Blocked by Time Constraint
**Scenario:** Job fits in resources but would complete after FCFS head reservation

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,60,0,pbatch,30
10,15,0,pbatch,45
```

**Expected:**
- Job 0: starts t=0, ends t=50
- Job 1: waits (FCFS head), reservation t=50, starts t=50, ends t=80
- Job 2: arrives t=10
  - Resources OK: 15 ≤ 20 ✓
  - Time check: 10+45=55 > 50 ✗ (would delay FCFS head)
  - BLOCKED, waits until t=50 or t=80

**Verifies:** Backfill constraint enforced (time)

---

### BF-3: Backfill Blocked by Resource Constraint
**Scenario:** Job arrives before FCFS head reservation but doesn't fit

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,90,0,pbatch,100
10,20,0,pbatch,30
```

**Expected:**
- Job 0: starts t=0, ends t=100
- Job 1: arrives t=10
  - Resources: 20 > 10 (free) ✗
  - BLOCKED by resources
  - Waits until t=100

**Verifies:** Resource constraint checked first

---

### BF-4: Multiple Jobs Backfill
**Scenario:** Multiple small jobs backfill while large job runs

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,70,0,pbatch,100
10,10,0,pbatch,30
20,10,0,pbatch,30
30,10,0,pbatch,30
```

**Expected:**
- Job 0: starts t=0, ends t=100 (70 nodes)
- Free: 30 nodes
- Job 1: starts t=10, ends t=40 (10 nodes, 20 free)
- Job 2: starts t=20, ends t=50 (10 nodes, 10 free)
- Job 3: starts t=30, ends t=60 (10 nodes, 0 free)
- All backfill simultaneously

**Verifies:** Multiple backfills can coexist

---

### BF-5: Sequential Backfills
**Scenario:** Jobs backfill one after another as resources free up

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,100
10,15,0,pbatch,20
20,15,0,pbatch,20
```

**Expected:**
- Job 0: starts t=0 (80 nodes, 20 free)
- Job 1: starts t=10, ends t=30 (15 nodes, 5 free)
- Job 2: arrives t=20
  - Resources: 15 > 5 ✗
  - Waits until t=30 (Job 1 ends)
  - Starts t=30, ends t=50

**Verifies:** Backfills can happen sequentially as resources free

---

### BF-6: Backfill with Exact Timing
**Scenario:** Backfill job completes exactly at FCFS head reservation

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,60,0,pbatch,30
10,15,0,pbatch,40
```

**Expected:**
- Job 0: starts t=0, ends t=50
- Job 1: FCFS head, reservation t=50
- Job 2: arrives t=10
  - Time check: 10+40=50 ≤ 50 ✓ (exactly at boundary)
  - BACKFILLS, starts t=10, ends t=50

**Verifies:** Boundary condition (≤ vs <)

---

### BF-7: No Backfill Opportunity (FCFS Head Fits)
**Scenario:** FCFS head can start immediately, no backfill needed

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,40,0,pbatch,50
10,30,0,pbatch,40
```

**Expected:**
- Job 0: starts t=0, ends t=50 (40 nodes, 60 free)
- Job 1: arrives t=10
  - Is FCFS head (only waiting job)
  - Fits: 30 ≤ 60 ✓
  - Starts immediately at t=10
  - NOT backfilling, just FCFS

**Verifies:** Distinguish between FCFS and backfill

---

### BF-8: Backfill Then FCFS Head Delayed
**Scenario:** Backfill succeeds, then FCFS head still waits (longer job)

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,60,0,pbatch,80
10,15,0,pbatch,20
```

**Expected:**
- Job 0: starts t=0, ends t=50
- Job 1: FCFS head, reservation t=50
- Job 2: arrives t=10, backfills (10+20=30 < 50), ends t=30
- Job 1: starts t=50, ends t=130 (still waits for Job 0, not Job 2)

**Verifies:** FCFS head reservation not affected by backfill completions

---

### BF-9: Backfill with Staggered Arrivals
**Scenario:** Jobs arrive at different times, some backfill, some don't

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,100
10,15,0,pbatch,30
20,15,0,pbatch,30
30,15,0,pbatch,70
```

**Expected:**
- Job 0: starts t=0, ends t=100 (20 free)
- Job 1: arrives t=10, backfills (10+30=40 < 100), starts t=10
- Job 2: arrives t=20, backfills (20+30=50 < 100), starts t=20
- Job 3: arrives t=30
  - Time check: 30+70=100 ≤ 100 ✓
  - Resources: 15 ≤ 20 ✓ (but Jobs 1,2 using 30, so really 20-30=-10)
  
Wait, need to recalculate:
- At t=20: Job 0 (80), Job 1 (15) = 95, free=5
- Job 2 needs 15 > 5, BLOCKED

Let me recalculate properly...

Actually, let me simplify this test.

---

### BF-9: Backfill with Multiple FCFS Heads
**Scenario:** Multiple large jobs waiting, small jobs backfill between them

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,80,0,pbatch,50
10,10,0,pbatch,20
```

**Expected:**
- Job 0: starts t=0, ends t=50 (FCFS head)
- Job 1: waits (FCFS head after Job 0), reservation t=50
- Job 2: arrives t=10, backfills (10+20=30 < 50), starts t=10
- Job 1: starts t=50, ends t=100

**Verifies:** Backfilling with multiple FCFS heads in queue

---

### BF-10: Long Backfill Candidate
**Scenario:** Small job in nodes but long duration

**Input:**
```csv
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,60,0,pbatch,30
10,5,0,pbatch,100
```

**Expected:**
- Job 0: starts t=0, ends t=50
- Job 1: FCFS head, reservation t=50
- Job 2: arrives t=10
  - Resources: 5 ≤ 20 ✓
  - Time: 10+100=110 > 50 ✗
  - BLOCKED despite small node count
  - Waits

**Verifies:** Duration matters as much as node count

---

## Test Organization

Create these test files:
```
tests/test_traces/correctness/
├── bf01_basic_success_input.csv
├── bf01_basic_success_reference.csv
├── bf02_blocked_time_input.csv
├── bf02_blocked_time_reference.csv
├── ...
├── bf10_long_duration_input.csv
└── bf10_long_duration_reference.csv
```

## Verification Procedure

### Step 1: Verify Oracle with Backfill Tests
```bash
# Generate reference outputs
for i in {01..10}; do
    python3 scripts/minimal_easy_oracle.py \
      tests/test_traces/correctness/bf${i}_*_input.csv \
      --nodes 100 --output bf${i}_reference.csv
done
```

### Step 2: Verify DR_EVT with Backfill Tests
```bash
# Run DR_EVT
for i in {01..10}; do
    ./build/simulator tests/test_traces/correctness/bf${i}_*_input.csv \
      --total_nodes 100 --trace_format simple --timestamp_format epoch \
      --duration_mode exact --outfile /tmp/bf${i}.out
    
    python3 scripts/compare_with_oracle.py \
      tests/test_traces/correctness/bf${i}_*_reference.csv /tmp/bf${i}.out
done
```

### Step 3: Cross-Validation (Both Pass 1-5)
If both oracle and DR_EVT pass all backfill tests:

```bash
echo "=== Cross-Validation Phase ==="
echo "Both implementations passed hand-traced + backfill tests"
echo "Now testing on large-scale trace..."

# Generate large trace
python3 scripts/generate_large_test.py --jobs 2000 --output large_test.csv

# Run oracle
python3 scripts/minimal_easy_oracle.py large_test.csv \
  --nodes 100 --output large_oracle.csv

# Run DR_EVT
./build/simulator large_test.csv \
  --total_nodes 100 --trace_format simple --timestamp_format epoch \
  --duration_mode exact --outfile large_dr_evt.csv

# Compare
python3 scripts/compare_with_oracle.py large_oracle.csv large_dr_evt.csv
```

**If large trace matches** → Both implementations are verified correct at scale

## Expected Results

After running full test suite:

```
✅ Hand-traceable tests (5 tests) - PASS
✅ Invariant checks - PASS  
✅ Contradiction tests - PASS
✅ Self-consistency - PASS
✅ Backfill correctness (10 tests) - PASS
✅ Cross-validation (all tests) - PASS
✅ Large-scale trace (2000 jobs) - PASS
```

**Result:** High confidence both implementations are correct

## Test Coverage Matrix

| Scenario | Test | Property Verified |
|----------|------|-------------------|
| Basic backfill | BF-1 | Small job can backfill |
| Time constraint | BF-2, BF-6, BF-10 | Duration limit enforced |
| Resource constraint | BF-3 | Node limit enforced |
| Multiple backfills | BF-4, BF-5 | Concurrent/sequential |
| FCFS vs backfill | BF-7 | Distinguish scenarios |
| Reservation integrity | BF-8 | FCFS not affected by backfills |
| Complex scenarios | BF-9 | Multiple FCFS heads |

## Creating the Tests

Let me create the actual test files...

```bash
#!/bin/bash
# generate_backfill_tests.sh

cd tests/test_traces/correctness

# BF-1: Basic backfill success
cat > bf01_basic_success_input.csv << 'EOF'
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,100
10,15,0,pbatch,30
EOF

# BF-2: Blocked by time
cat > bf02_blocked_time_input.csv << 'EOF'
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,60,0,pbatch,30
10,15,0,pbatch,45
EOF

# BF-3: Blocked by resources
cat > bf03_blocked_resources_input.csv << 'EOF'
job_submit_time,num_nodes,exit_status,queue,time_limit
0,90,0,pbatch,100
10,20,0,pbatch,30
EOF

# BF-4: Multiple backfills
cat > bf04_multiple_backfill_input.csv << 'EOF'
job_submit_time,num_nodes,exit_status,queue,time_limit
0,70,0,pbatch,100
10,10,0,pbatch,30
20,10,0,pbatch,30
30,10,0,pbatch,30
EOF

# BF-5: Sequential backfills
cat > bf05_sequential_backfill_input.csv << 'EOF'
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,100
10,15,0,pbatch,20
20,15,0,pbatch,20
EOF

# BF-6: Exact timing boundary
cat > bf06_exact_timing_input.csv << 'EOF'
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,60,0,pbatch,30
10,15,0,pbatch,40
EOF

# BF-7: FCFS not backfill
cat > bf07_fcfs_not_backfill_input.csv << 'EOF'
job_submit_time,num_nodes,exit_status,queue,time_limit
0,40,0,pbatch,50
10,30,0,pbatch,40
EOF

# BF-8: Backfill then FCFS delayed
cat > bf08_backfill_fcfs_delayed_input.csv << 'EOF'
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,60,0,pbatch,80
10,15,0,pbatch,20
EOF

# BF-9: Multiple FCFS heads
cat > bf09_multiple_fcfs_input.csv << 'EOF'
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,80,0,pbatch,50
10,10,0,pbatch,20
EOF

# BF-10: Long duration blocks backfill
cat > bf10_long_duration_input.csv << 'EOF'
job_submit_time,num_nodes,exit_status,queue,time_limit
0,80,0,pbatch,50
0,60,0,pbatch,30
10,5,0,pbatch,100
EOF

echo "Backfill test inputs created"
```

## Summary

**Test Progression:**
1-4: Basic verification (hand-traced, invariants, contradictions, self-consistency)
5: **Backfill correctness suite** (10 systematic tests)
6: Cross-validation (run all tests through both implementations)
7: Large-scale validation (2000 job trace)

**Result:** Comprehensive verification that both implementations are correct
