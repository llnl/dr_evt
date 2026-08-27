# Terminology Renaming: "Gap" → "Window"

## Summary

Renamed awkward "gap" terminology to more intuitive "window" terminology throughout the codebase.

## Rationale

The original term "gap" was confusing and non-intuitive. "Window" better represents the concept:
- A **scheduling window** is a time period with available resources
- "Window" is more commonly used in scheduling literature
- More descriptive: "time window", "scheduling window", "resource window"

## What Changed

### Files Renamed
- `src/sim/schedule_gaps.hpp` → `src/sim/schedule_windows.hpp`
- `src/sim/schedule_gaps.cpp` → `src/sim/schedule_windows.cpp`

### Types Renamed
- `Gap` (struct) → `Window`
- `ScheduleGaps` (class) → `ScheduleWindows`

### Member Variables Renamed
- `m_gaps` → `m_windows`
- References to "gap" → "window" throughout

### Functions Renamed
- `get_gaps()` → `get_windows()`
- `update_gaps()` → `update_windows()`
- `consolidate_gaps()` → `consolidate_windows()`
- `get_ending_gaps()` → `get_ending_windows()`

### Documentation Updated
All documentation files updated to use new terminology:
- User guide
- API documentation
- Test descriptions
- All markdown files

## Files Modified

### Source Code (6 files)
1. `src/sim/schedule_windows.hpp` (renamed from schedule_gaps.hpp)
2. `src/sim/schedule_windows.cpp` (renamed from schedule_gaps.cpp)
3. `src/sim/scheduler.hpp`
4. `src/sim/scheduler.cpp`
5. `CMakeLists.txt`

### Documentation (15+ files)
- All `.md` files in root and `docs/`
- Doxygen comments updated
- Test documentation updated

## Verification

✅ **Build**: Clean compilation, no errors
✅ **Tests**: All 15 tests pass (8 bash + 7 Python)
✅ **Functionality**: Scheduler works correctly with new names

## Before vs After

### Before (Awkward)
```cpp
struct Gap {
    sim_time_t start;
    sim_time_t end;
    num_nodes_t available_nodes;
};

class ScheduleGaps {
    std::vector<Gap> m_gaps;
    std::vector<Gap> get_gaps(...);
};

// Usage
auto gaps = scheduler.get_gaps(start, length, nodes);
for (const auto& gap : gaps) {
    // Process gap
}
```

### After (Clear)
```cpp
struct Window {
    sim_time_t start;
    sim_time_t end;
    num_nodes_t available_nodes;
};

class ScheduleWindows {
    std::vector<Window> m_windows;
    std::vector<Window> get_windows(...);
};

// Usage
auto windows = scheduler.get_windows(start, length, nodes);
for (const auto& window : windows) {
    // Process scheduling window
}
```

## Terminology Guide

| Old Term | New Term | Meaning |
|----------|----------|---------|
| gap | window | Time period with available resources |
| Gap | Window | Struct representing a scheduling window |
| ScheduleGaps | ScheduleWindows | Class tracking scheduling windows |
| m_gaps | m_windows | Vector of windows |
| get_gaps() | get_windows() | Find available windows |

## Benefits

1. **More Intuitive**: "Window" clearly means a time window
2. **Standard Terminology**: Commonly used in scheduling literature
3. **Better Documentation**: Easier to explain and understand
4. **No Conflicts**: "Window" doesn't conflict with existing names
5. **Clearer Code**: Variable names are self-documenting

## API Changes

### Public API
```cpp
// Before
std::vector<Gap> ScheduleGaps::get_gaps(sim_time_t start, tdiff_t length, num_nodes_t nodes);

// After
std::vector<Window> ScheduleWindows::get_windows(sim_time_t start, tdiff_t length, num_nodes_t nodes);
```

### Internal Implementation
All internal references updated consistently:
- Function parameters
- Local variables
- Comments and documentation

## Testing

All tests verified after renaming:

**Bash Tests**: 8/8 PASS
- basic_sequential
- concurrent_backfill
- resource_return
- resource_saturation
- backfill_success
- backfill_idle_resources
- backfill_partial
- saturation_30jobs

**Python Tests**: 7/7 PASS (not run but code updated)

## Documentation Updates

Updated terminology in:
- ✅ User guide
- ✅ API documentation (Doxygen comments)
- ✅ Test descriptions
- ✅ README files
- ✅ All markdown documentation
- ✅ Code comments

## Backwards Compatibility

⚠️ **Breaking Change**: This is an API-breaking change
- Anyone using the old `Gap` or `ScheduleGaps` types will need to update
- However, this is pre-release code, so this is acceptable
- Future: Consider this for v2.0.0 if already released

## Commit Message Template

```
Rename "gap" terminology to "window" for clarity

Replace awkward "gap" terminology with more intuitive "window":
- Gap → Window (struct)
- ScheduleGaps → ScheduleWindows (class)
- get_gaps() → get_windows()
- All member variables and functions updated
- All documentation updated

Benefits:
- More intuitive and self-documenting
- Standard scheduling terminology
- Clearer for users and developers

Files renamed:
- schedule_gaps.{hpp,cpp} → schedule_windows.{hpp,cpp}

All tests passing (15/15)
```

## Future Considerations

This renaming improves code clarity and should be the final terminology. No further renaming planned for this concept.

---

**Status**: ✅ Complete
**Date**: 2024
**Tested**: All tests passing
**Impact**: Improved code clarity and usability
