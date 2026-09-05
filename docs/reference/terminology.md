# Terminology - Clarification

## The Confusion

Some terms are ambiguous and should be avoided or used with clear context.

## Correct Terminology

### 1. Reference Implementation
**What it is:** Python implementation of EASY backfilling used to verify C++ implementation
**Call it:** "Reference implementation" or "Python reference"
**File naming:** `*_reference.csv` (output from reference implementation)
**Script:** `scripts/python_reference_scheduler.py` (historical name, kept for now)

### 2. Run Time Mode

**`run_time_mode`** - Controls how the job's actual execution length is
determined in simulation mode.

- `actual` (default) - Read the job's real run time from the trace's
  `actual_run_time` column (or aliases: `duration`, `actual_duration`, `run_time`)
- `distribution` - Sample from a statistical distribution
  (`normal`/`lognormal`/`uniform`) around `time_limit * scale`;
  `normal`/`lognormal` are capped so a sample can never exceed
  `time_limit`, since a real HPC scheduler kills a job at its stated limit
- `limit` - Jobs run for exactly their `time_limit` (unrealistic, for debugging only)

**Note:** The scheduler uses `time_limit` as the best estimator for planning decisions,
regardless of `run_time_mode`.

### 3. Reference Implementation vs Expected Output

**Be explicit:**
- Python reference implementation → "reference implementation"
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
- `scripts/python_reference_scheduler.py` - Reference implementation

**Code constants:**
```cpp
enum class RunTimeMode {
    ACTUAL,       // Read job's actual run time from trace (default)
    DISTRIBUTION, // Sample from statistical distribution
    LIMIT         // Job runs for exactly time_limit (debugging only)
};
```

**Documentation:**
- Use "reference implementation" when referring to the Python scheduler
- Use "reference output" or "expected output" for test validation files

## Examples

### ✅ Clear Usage
```bash
# Generate reference output
python3 scripts/python_reference_scheduler.py input.csv

# Compare DR_EVT output against reference
diff reference.csv output.csv
```

## Summary Table

| Concept | Correct Term | File Suffix | CLI Flag |
|---------|--------------|-------------|----------|
| Python implementation | Reference implementation | `_reference.csv` | N/A |
| How the job's actual run time is determined | Run time mode | N/A | `-r, --run_time_mode {actual\|distribution\|limit}` |
| Expected output | Reference output | `_reference.csv` | N/A |
| Test input | Input trace | `_input.csv` | N/A |

## Migration Notes

**Files to rename (optional):**
- None currently - our current `*_reference.csv` naming is correct
- Script names (`python_reference_scheduler.py`) are historical but acceptable

**Documentation:**
- Use "reference implementation" when referring to Python scheduler

## Why This Matters

**Clear terminology prevents confusion:**
- "Run the reference implementation on this trace"
- "Compare DR_EVT output against reference"

**Avoid:**
- Vague terms like "oracle" without context

## Verification Terminology

**Correct:**
- "Verify C++ implementation against Python reference"
- "Compare output with reference implementation results"

**Instead:**
- "Verify against reference implementation"
- "Compare with expected output"
