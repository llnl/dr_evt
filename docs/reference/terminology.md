# Terminology - Clarification

## The Confusion

We've been using "oracle" inconsistently:
1. **Oracle** = Python reference implementation  
2. **Oracle** = Exact job duration mode (vs realistic)

This is confusing! Let's fix it.

## Correct Terminology

### 1. Reference Implementation
**What it is:** Python implementation of EASY backfilling used to verify C++ implementation  
**Call it:** "Reference implementation" or "Python reference"  
**File naming:** `*_reference.csv` (output from reference implementation)  
**Script:** `scripts/python_reference_scheduler.py` (historical name, kept for now)

### 2. Job Duration Modes
**What it is:** How job durations are determined in simulation  

**Mode 1: Exact Duration**
- Jobs run for exactly their `time_limit`
- Scheduler plans for `time_limit`
- Job completes at `time_limit`
- **Call it:** "Exact duration mode" or "USE_ACTUAL"
- **NOT "oracle"**

**Mode 2: Realistic Duration**  
- Users provide `time_limit` (often overestimate)
- Job actual runtime may be less
- Scheduler plans for `time_limit` (conservative)
- Job completes at actual runtime
- **Call it:** "Realistic duration mode" or "USE_LIMIT"

### 3. Oracle (Ambiguous - Avoid)
**Problem:** "Oracle" could mean:
- Reference implementation (Python)
- Perfect knowledge (exact durations)
- Ground truth (correct answer)

**Solution:** Be explicit:
- Python reference implementation → "reference"
- Exact duration → "exact duration mode"
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

### Duration Mode Variants
```
{test_name}_exact_duration.csv     # Exact mode
{test_name}_realistic_duration.csv # Realistic mode
```

## Code/Script Names

**Scripts:**
- `scripts/python_reference_scheduler.py` - Reference implementation (historical name)
- `scripts/compare_with_oracle.py` - Comparison tool (historical name)

**Better names (if renaming):**
- `scripts/reference_implementation.py`
- `scripts/compare_outputs.py`

**Code constants:**
```cpp
enum class RuntimeEstimateMode {
    USE_ACTUAL,  // Exact duration (scheduler knows actual runtime)
    USE_LIMIT    // Realistic (scheduler uses time_limit)
};
```

**Documentation:**
- "Exact duration mode" not "oracle mode"
- "Reference implementation" not "oracle"

## Examples

### ❌ Ambiguous (Old)
```bash
# Generate oracle output
python3 oracle.py input.csv

# Run in oracle mode
./simulator --oracle-mode input.csv

# Compare against oracle
compare_with_oracle.py oracle.csv output.csv
```

### ✅ Clear (New)
```bash
# Generate reference output
python3 scripts/python_reference_scheduler.py input.csv

# Run in exact duration mode  
./simulator --duration_mode exact input.csv

# Compare against reference
python3 scripts/compare_with_oracle.py reference.csv output.csv
```

## Summary Table

| Concept | Correct Term | File Suffix | CLI Flag |
|---------|--------------|-------------|----------|
| Python implementation | Reference implementation | `_reference.csv` | N/A |
| Exact job runtime | Exact duration mode | `_exact_duration.csv` | `--duration_mode exact` |
| Realistic runtime | Realistic duration mode | `_realistic_duration.csv` | `--duration_mode=limit` |
| Expected output | Reference output | `_reference.csv` | N/A |
| Test input | Input trace | `_input.csv` | N/A |

## Migration Notes

**Files to rename (optional):**
- None currently - our current `*_reference.csv` naming is correct
- Script names (`python_reference_scheduler.py`) are historical but acceptable

**Documentation to update:**
- Replace "oracle mode" → "exact duration mode"
- Replace "oracle" → "reference implementation" when referring to Python
- Keep "oracle" only in historical script names

## Why This Matters

**Clear terminology prevents confusion:**
- "Run the reference implementation on this trace"
- "Test in exact duration mode"
- "Compare DR_EVT output against reference"

**Not:**
- "Run the oracle" (which oracle?)
- "Use oracle mode" (what does that mean?)

## Verification Terminology

**Correct:**
- "Verify C++ implementation against Python reference"
- "Run simulation in exact duration mode"
- "Compare output with reference implementation results"

**Avoid:**
- "Verify against oracle" (ambiguous)
- "Oracle verification" (which kind?)
- "Oracle mode" (exact duration? reference implementation?)
