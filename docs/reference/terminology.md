# Terminology - Clarification

## The Confusion

We've been using "oracle" inconsistently:
1. **Oracle** = Python reference implementation
2. **Oracle** = Scheduler using perfect knowledge of job run times (`duration_mode=actual`)

This is confusing! Let's fix it.

## Correct Terminology

### 1. Reference Implementation
**What it is:** Python implementation of EASY backfilling used to verify C++ implementation
**Call it:** "Reference implementation" or "Python reference"
**File naming:** `*_reference.csv` (output from reference implementation)
**Script:** `scripts/python_reference_scheduler.py` (historical name, kept for now)

### 2. Duration Mode vs. Run Time Mode

There are two distinct, related concepts here, and they used to have
swapped names - `run_time_mode` used to control the scheduler's own
estimate, and `duration_mode` used to control the observed execution
length. Both were renamed to fix this:

**`duration_mode`** - the scheduler's own job-length *estimate*, used
for reservation/backfill planning decisions. This is what the scheduler
*considers* when deciding how long a running job will occupy its nodes.

- `limit` (default) - pessimistic/realistic: the scheduler uses the
  job's stated `time_limit` for planning, the same information a real
  scheduler has. Reservation estimates can be wrong relative to how
  long a job actually runs.
- `actual` - omniscient/oracle: the scheduler uses the job's real,
  observed run time for planning, as if it had perfect knowledge of the
  future. Useful for comparison studies against an optimal schedule,
  not realistic. When active, `run_time_mode` (below) is ignored
  entirely - the real, historical run time is used directly regardless
  of what `run_time_mode` is separately set to.

**`run_time_mode`** - how the job's actual, observed execution length is
*determined* in simulation mode. This is what's actually observed when
the job runs, and only matters when `duration_mode=limit` - there's no
"real" run time to fall back on in that case, so the simulator itself
must decide how long each job actually takes.

- `exact` (default) - jobs run for exactly their `time_limit`
- `column` - read the job's real run time from the trace's own
  `actual_run_time` column
- `distribution` - sample from a statistical distribution
  (`normal`/`lognormal`/`uniform`) around `time_limit * scale`;
  `normal`/`lognormal` are capped so a sample can never exceed
  `time_limit`, since a real HPC scheduler kills a job at its stated
  limit

**Do NOT call either of these "oracle" or "exact" alone** - both words
are ambiguous between the two concepts. Say "duration_mode=actual"
(the scheduler's planning) or "run_time_mode=column" (how execution
length is determined), not "oracle mode" or "exact mode" bare.

### 3. Oracle (Ambiguous - Avoid)
**Problem:** "Oracle" could mean:
- Reference implementation (Python)
- Perfect knowledge (scheduler planning with `duration_mode=actual`)
- Ground truth (correct answer)

**Solution:** Be explicit:
- Python reference implementation → "reference"
- Scheduler with perfect knowledge → "duration_mode=actual" or "omniscient scheduling"
- Expected output → "reference output" or "expected output"

## File Naming Convention

### Test Inputs
```
{test_name}_input.csv
```
Example: `easy_5jobs_input.csv`

### Reference Outputs (from Python)
```
{test_name}_reference.csv
```
Example: `easy_5jobs_reference.csv`

## Code/Script Names

**Scripts:**
- `scripts/python_reference_scheduler.py` - Reference implementation (historical name)
- `scripts/compare_with_oracle.py` - Comparison tool (historical name)

**Better names (if renaming):**
- `scripts/reference_implementation.py`
- `scripts/compare_outputs.py`

**Code constants:**
```cpp
enum class DurationEstimateMode {
    USE_LIMIT,   // Realistic: scheduler plans using time_limit
    USE_ACTUAL   // Omniscient: scheduler plans using the job's real run time
};

enum class RunTimeMode {
    EXACT,        // Job runs for exactly time_limit
    FROM_COLUMN,  // Job's real run time read from the trace's actual_run_time column
    DISTRIBUTION  // Job's run time sampled from a statistical distribution
};
```

**Documentation:**
- "duration_mode=actual" or "omniscient scheduling" not "oracle mode"
- "Reference implementation" not "oracle"

## Examples

### ❌ Ambiguous (Old)
```bash
# Generate oracle output
python3 oracle.py input.csv

# Run in oracle mode
${CMAKE_INSTALL_PREFIX}/bin/simulator --oracle-mode input.csv

# Compare against oracle
compare_with_oracle.py oracle.csv output.csv
```

### ✅ Clear (New)
```bash
# Generate reference output
python3 scripts/python_reference_scheduler.py input.csv

# Run with omniscient scheduler planning
${CMAKE_INSTALL_PREFIX}/bin/simulator --duration_mode actual input.csv  # Oracle mode

# Compare against reference
python3 scripts/compare_with_oracle.py reference.csv output.csv
```

## Summary Table

| Concept | Correct Term | File Suffix | CLI Flag |
|---------|--------------|-------------|----------|
| Python implementation | Reference implementation | `_reference.csv` | N/A |
| Scheduler's own planning estimate | Duration mode | N/A | `--duration_mode {limit\|actual}` |
| How the job's real run time is determined | Run time mode | N/A | `--run_time_mode {exact\|column\|distribution}` |
| Expected output | Reference output | `_reference.csv` | N/A |
| Test input | Input trace | `_input.csv` | N/A |

## Migration Notes

**Files to rename (optional):**
- None currently - our current `*_reference.csv` naming is correct
- Script names (`python_reference_scheduler.py`) are historical but acceptable

**Documentation to update:**
- Replace "oracle mode" → "duration_mode=actual" or "omniscient scheduling"
- Replace "oracle" → "reference implementation" when referring to Python
- Keep "oracle" only in historical script names

## Why This Matters

**Clear terminology prevents confusion:**
- "Run the reference implementation on this trace"
- "Run with duration_mode=actual for omniscient scheduling"
- "Compare DR_EVT output against reference"

**Not:**
- "Run the oracle" (which oracle?)
- "Use oracle mode" (what does that mean?)

## Verification Terminology

**Correct:**
- "Verify C++ implementation against Python reference"
- "Run simulation with duration_mode=actual"
- "Compare output with reference implementation results"

**Avoid:**
- "Verify against oracle" (ambiguous)
- "Oracle verification" (which kind?)
- "Oracle mode" (perfect scheduling knowledge? reference implementation?)
