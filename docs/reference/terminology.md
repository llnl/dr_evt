# Terminology - Clarification

## The Confusion

Some terms are ambiguous and should be avoided or used with clear context.

## Correct Terminology

### 1. Reference Implementation
**What it is:** Python implementation of EASY backfilling used to verify C++ implementation
**Call it:** "Reference implementation" or "Python reference"
**File naming:** none fixed - output is generated on demand during test
development and diffed against the fixed `.expected_output.csv`/
`.expected_resources.csv` files in `tests/test_traces/scheduler_correctness/`
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

The comprehensive test suite (`tests/test_traces/scheduler_correctness/`) names
each test's files by a shared `{test_name}` stem, not the
`_input`/`_reference` suffixes this document used to describe:

```
{test_name}.csv                    # input trace
{test_name}.expected_output.csv    # expected simulated-trace output (machine-comparable)
{test_name}.expected_resources.csv # expected resource-usage trace (machine-comparable)
{test_name}.answer.json            # expected schedule/statistics, structured
{test_name}.construction.md        # human-readable description of the scenario and why it's expected to behave this way
```

Example: `01_backfill_allowed.csv`, `01_backfill_allowed.expected_output.csv`, etc.

`scripts/python_reference_scheduler.py` output being diffed against
during test development doesn't have a fixed naming convention of its
own - it's generated on demand and compared against whichever of the
files above is relevant, not saved as a permanent fixture itself.

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
| Python implementation | Reference implementation | N/A (generated on demand, not a saved fixture) | N/A |
| How the job's actual run time is determined | Run time mode | N/A | `-r, --run_time_mode {actual\|distribution\|limit}` |
| Expected output | Reference output | `.expected_output.csv` / `.expected_resources.csv` | N/A |
| Test input | Input trace | `.csv` | N/A |

## Migration Notes

**Files to rename (optional):**
- None currently - our current `.expected_output.csv`/`.expected_resources.csv` naming is correct
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
