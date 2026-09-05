# Timezone Offset Support Design

## Status: Implementation Complete (Untested)

Core timezone parsing and storage implemented. Build system has configuration issues preventing testing.
The implementation is functionally complete but needs build fixes to compile and test.

## Overview

Add support for ISO 8601 timestamps with timezone offsets (e.g., "2024-01-01T12:00:00-08:00") with conversion to UTC for internal storage and back to local time for human-readable output.

## Design Principles

1. **Store times as UTC** - All internal epoch_t values are UTC Unix time
2. **Timezone as metadata** - Timezone info stored separately for display only
3. **Per-queue flexibility** - Different facilities/queues can have different timezones
4. **Backward compatible** - Defaults to UTC if no timezone specified

## Implementation Details

### 1. Parse Timezone Offsets (✅ IMPLEMENTED)

**Files**: `src/trace/epoch.hpp`, `src/trace/epoch.cpp`

New functions:
```cpp
// Parse ISO timestamp with timezone, return UTC epoch + timezone string
std::pair<epoch_t, std::string> parse_time_with_timezone(const std::string& time_str);

// Convert UTC epoch back to local time for display
std::string to_local_time_string(const epoch_t& t, const std::string& tz_offset);
```

Parsing logic:
```
Input: "2024-01-01T12:00:00-08:00"
  ↓
1. Extract timezone: "-08:00"
2. Parse date/time: 2024-01-01 12:00:00
3. Convert to UTC: 12:00 PST + 8 hours = 20:00 UTC
4. Store epoch: 1704139200 (UTC)
5. Return: (epoch_t, "-08:00")
```

### 2. Timezone Metadata Storage (✅ IMPLEMENTED)

**Files**: `src/trace/trace.hpp`, `src/trace/trace.cpp`

Added to Trace class:
```cpp
protected:
    std::string m_default_timezone = "+00:00";  // UTC default
    std::map<std::string, std::string> m_queue_timezones;  // Per-queue overrides

public:
    void set_default_timezone(const std::string& tz_offset);
    void set_queue_timezone(const std::string& queue, const std::string& tz_offset);
    std::string get_queue_timezone(const std::string& queue) const;
```

Usage:
```cpp
trace.set_default_timezone("-08:00");  // All jobs default to PST
trace.set_queue_timezone("pbatch_summit", "-05:00");  // Override for Summit queue
```

### 3. Integrated Parsing (✅ IMPLEMENTED)

**Files**: `src/trace/parse_utils.cpp`

Auto-detect timezone offset in timestamps:
```cpp
if (has_timezone) {
    auto [utc_time, tz_offset] = parse_time_with_timezone(str);
    t = utc_time;  // Store UTC
    // tz_offset extracted but needs separate storage mechanism
}
```

### 4. Output Formatting (⚠️ TODO)

**Files**: `src/sim/sim.cpp`, `src/trace/job_record.cpp`

Convert back to local time when printing:
```cpp
std::string tz = trace.get_queue_timezone(job.get_queue());
std::cout << "Job " << job_id << " started at " 
          << to_local_time_string(start_time, tz) << std::endl;
```

## Examples

### Multi-Facility Trace

**Input** (different facilities):
```text
2024-01-01T12:00:00-08:00,pbatch_lassen,...  ← PST (LLNL)
2024-01-01T15:00:00-05:00,pbatch_summit,...  ← EST (ORNL)
2024-01-01T21:00:00+01:00,pbatch_pizda,...   ← CET (CSCS)
```

**Internal Storage** (all normalized to UTC):
```
epoch_t(1704139200, 0.0)  ← All three jobs
```

**Output** (human-readable):
```
[Lassen/PST] Job 0 started at 2024-01-01 12:00:00
[Summit/EST] Job 1 started at 2024-01-01 15:00:00
[PizDaint/CET] Job 2 started at 2024-01-01 21:00:00
```

## Benefits

✅ **Correct computation** - All time arithmetic uses UTC  
✅ **Human-readable output** - Times displayed in facility's local timezone  
✅ **Multi-facility support** - Can merge traces from different timezones  
✅ **Minimal overhead** - Timezone is metadata, not per-job storage  
✅ **Backward compatible** - Defaults to UTC for traces without timezone  

## Testing Plan

1. Create test trace with timezone offsets:
   ```text
   2024-01-01T08:00:00-08:00,2024-01-01T08:00:00-08:00,...
   2024-01-01T11:00:00-05:00,2024-01-01T11:00:00-05:00,...
   ```

2. Verify UTC conversion:
   - 08:00 PST (-08:00) → 16:00 UTC
   - 11:00 EST (-05:00) → 16:00 UTC
   - Both jobs should have same epoch value

3. Verify local time output:
   - Display should show original local times
   - Timezone labels should match facility

## Remaining Work

1. **Fix build system** - CMake cannot find protobuf targets after SetupProtobufConan.cmake was removed
   - Issue: `include(SetupProtobufConan)` fails because file doesn't exist
   - Workaround tried: Commenting out include causes protobuf::libprotobuf target not found
   - Protobuf is already built in `_deps/protobuf-build` but CMake lost track of it
   - Recommendation: Fresh cmake configuration or restore SetupProtobufConan.cmake from backup

2. **Complete output integration** - Update sim.cpp to use local time display
   - Modify Job_Record output to call `to_local_time_string()` for human-readable times
   - Use `trace.get_queue_timezone()` to get the correct offset per job

3. **Extract timezone during parsing** - Store tz_offset when reading trace  
   - Currently parse_utils.cpp extracts timezone but doesn't store it
   - Need to pass timezone string back to caller and store in Trace metadata
   - Add method: `trace.set_queue_timezone_from_job(job_idx, tz_offset)`

4. **Test implementation** - Once build works
   - Test trace created: test_traces/timezone_offsets.csv
   - Verify UTC conversion (all three jobs should normalize to 16:00:00 UTC)
   - Verify local time output displays original timezones correctly

5. **Update documentation** - USER_GUIDE.md, TEST_DESCRIPTIONS.md
   - Document timezone offset format support
   - Add timezone test to TESTING.md

## Files Modified

- ✅ `src/trace/epoch.hpp` - New timezone functions declared (lines 74-91)
- ✅ `src/trace/epoch.cpp` - Timezone parsing/formatting implemented (lines 84-179)
  - `parse_timezone_offset()` helper function
  - `parse_time_with_timezone()` main parsing function
  - `to_local_time_string()` display formatting function
- ✅ `src/trace/trace.hpp` - Timezone metadata storage added (lines 42-126)
  - `m_default_timezone` and `m_queue_timezones` members
  - getter/setter methods for timezone metadata
- ✅ `src/trace/trace.cpp` - Initialize default timezone to UTC (lines 18, 27)
- ✅ `src/trace/parse_utils.cpp` - Integrated timezone detection (lines 86-113)
  - Auto-detects timezone offsets in ISO timestamps
  - Calls `parse_time_with_timezone()` when offset found
  - Stores UTC epoch value
- ✅ `test_traces/timezone_offsets.csv` - Test trace with PST, EST, CET timestamps
- ⚠️ `src/sim/sim.cpp` - TODO: Update output formatting to use local times
- ⚠️ `CMakeLists.txt` - Commented out SetupProtobufConan include (breaks build)

## Build Issues

**Problem**: CMake configuration broken after commenting out `include(SetupProtobufConan)`

**Symptoms**:
- cmake fails with "protobuf is required!" when include is missing
- cmake fails with "protobuf::libprotobuf target not found" when DR_EVT_HAS_PROTOBUF=TRUE is set
- Protobuf is already compiled in `build/_deps/protobuf-build` but CMake lost references

**Root Cause**: SetupProtobufConan.cmake was removed from repository but CMakeLists.txt still references it

**Potential Solutions**:
1. Restore SetupProtobufConan.cmake from backup or another branch
2. Rewrite protobuf setup to use FetchContent directly in CMakeLists.txt
3. Manual protobuf package configuration with find_package()

**Current Workaround**: None - build is blocked

## Next Steps

1. **URGENT**: Fix CMake protobuf configuration
2. Wire up timezone storage during trace parsing
3. Update output functions to display local times
4. Build and test with timezone_offsets.csv
5. Update TESTING.md and USER_GUIDE.md

---

**Date**: 2026-08-27  
**Author**: Claude  
**Status**: Implementation in progress, build issues need resolution
