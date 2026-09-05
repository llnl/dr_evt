# Simulation vs Replay Modes - Design

**Date**: 2026-08-27

## Problem Statement

Current implementation conflates two distinct modes:
1. **Replay mode**: Re-execute historical trace with known start/end times
2. **Simulation mode**: Scheduler computes start times, actual duration must be determined

The trace parser currently requires `begin_time` and `end_time` columns even in simulation mode, leading to logical inconsistencies where the simulator has "future knowledge" of actual job durations.

## Design Goals

1. **Clear separation** between replay and simulation modes
2. **Flexible trace formats** - parser detects mode from columns present
3. **Multiple run time models** for simulation (limit, sampled, from-column)
4. **No redundant data** - don't store both duration and end_time

## Trace Format Specifications

### Replay Mode Format

**Required columns:**
```
job_submit_time, begin_time, end_time, num_nodes, exit_status, queue, time_limit
```

**Alternative (duration instead of end_time):**
```
job_submit_time, begin_time, duration, num_nodes, exit_status, queue, time_limit
```

**Semantics:**
- All times are **historical actuals** from real execution
- Simulator replays these times exactly
- Scheduler is NOT invoked (or invoked but decisions are ignored)

**Parser behavior:**
- If `begin_time` AND (`end_time` OR `duration`) exist → **Replay mode**
- Jobs start at `begin_time` from trace
- Duration = `end_time - begin_time` OR `duration` column

### Simulation Mode Format

**Required columns:**
```
job_submit_time, num_nodes, exit_status, queue, time_limit
```

**Optional column (for duration):**
```
actual_duration
```

**Semantics:**
- `job_submit_time`: When job arrives in queue
- `time_limit`: User's estimate (scheduler uses for planning/reservations)
- `actual_duration`: Ground truth runtime (if column present)
- `begin_time`: Computed by scheduler
- `end_time`: Computed as `begin_time + actual_duration`

**Parser behavior:**
- If `begin_time` is ABSENT → **Simulation mode**
- Scheduler computes start times
- Actual duration determined by mode (see below)

## Duration Determination in Simulation Mode

The simulator supports three methods for determining actual job duration, controlled by `--run_time_mode` option:

### Mode 1: Actual (`--run_time_mode actual`)

**Requires:** `actual_run_time` column in trace

```text
job_submit_time,num_nodes,queue,time_limit,actual_run_time
0,80,pbatch,100,87
10,15,pbatch,20,18
```

**Behavior:**
- Read `actual_run_time` directly from trace
- Use for scheduling completion event
- Allows testing with realistic duration profiles

**Use case:** 
- What-if analysis on historical workload
- Test different scheduling policies on same jobs with same durations

### Mode 2: Limit (`--run_time_mode limit`)

**Requires:** Only `time_limit` column

```text
job_submit_time,num_nodes,queue,time_limit
0,80,pbatch,100
10,15,pbatch,20
```

**Behavior:**
```cpp
actual_run_time = time_limit  // Jobs run for full limit
```

**Use case:**
- Worst-case scenario (all jobs use full allocation)
- Simplest simulation model
- Testing scheduler logic without duration uncertainty

### Mode 3: Distribution (`--run_time_mode distribution`)

**Requires:** Distribution parameters

**Command line:**
```bash
--run_time_mode distribution \
--run_time_distribution normal \
--run_time_scale 0.8 \
--run_time_stddev 0.2
```

**Behavior (Normal distribution):**
```cpp
actual_run_time = sample_normal(
    mean = time_limit * scale_factor,
    stddev = time_limit * stddev_factor
)
actual_duration = max(0, actual_duration)  // Clamp to positive
```

**Behavior (Lognormal distribution):**
```cpp
// Lognormal parameters
mu = log(time_limit * scale_factor)
sigma = stddev_factor

actual_duration = sample_lognormal(mu, sigma)
```

**Distribution options:**
- `normal`: N(time_limit * f, time_limit * s)
- `lognormal`: LogNormal(log(time_limit * f), s)
- `uniform`: Uniform(time_limit * f_min, time_limit * f_max)

**Use case:**
- Realistic modeling of user estimation errors
- Study impact of estimation quality on scheduler
- Stochastic workload generation

## Data Structure Changes

### Job_Record Modifications

**Add new fields:**
```cpp
class Job_Record {
  private:
    epoch_t m_t_begin;        // Start time (replay: from trace, sim: from scheduler)
    epoch_t m_t_end;          // End time (replay: from trace, sim: computed)
    
    // NEW FIELDS:
    tdiff_t m_time_limit;     // User estimate (for scheduler planning)
    tdiff_t m_actual_duration; // Actual runtime (ground truth)
    
    bool m_is_simulated;      // True if times computed by scheduler
```

**Methods:**
```cpp
// Getters
tdiff_t get_time_limit() const { return m_time_limit; }
tdiff_t get_actual_duration() const { return m_actual_duration; }
tdiff_t get_exec_time() const { return m_actual_duration; }  // Alias for compatibility

// Setters (for simulation mode)
void set_begin_time(const epoch_t& t) { m_t_begin = t; m_is_simulated = true; }
void set_actual_duration(tdiff_t d) { m_actual_duration = d; }
void compute_end_time() { 
    m_t_end = m_t_begin; 
    m_t_end.first += static_cast<time_t>(m_actual_duration);
    m_t_end.second += (m_actual_duration - static_cast<time_t>(m_actual_duration));
}
```

### Data_Columns Modifications

**Detect mode from columns present:**
```cpp
enum class TraceMode {
    REPLAY,     // Has begin_time and (end_time OR duration)
    SIMULATION  // Missing begin_time
};

class Data_Columns {
  private:
    TraceMode m_trace_mode;
    
    // Column indices (optional)
    std::optional<col_no_t> m_begin_time_idx;
    std::optional<col_no_t> m_end_time_idx;
    std::optional<col_no_t> m_duration_idx;
    std::optional<col_no_t> m_actual_duration_idx;
    
  public:
    TraceMode get_trace_mode() const { return m_trace_mode; }
    bool has_column(const std::string& name) const;
```

**Initialize based on header:**
```cpp
bool Data_Columns::init_trace_file(const std::string& fname) {
    // Read header, parse column names
    // ...
    
    // Detect mode
    if (has_column("begin_time")) {
        m_trace_mode = TraceMode::REPLAY;
        // Require end_time OR duration
        if (!has_column("end_time") && !has_column("duration")) {
            throw std::invalid_argument("Replay mode requires end_time or duration column");
        }
    } else {
        m_trace_mode = TraceMode::SIMULATION;
        // Require time_limit
        if (!has_column("time_limit")) {
            throw std::invalid_argument("Simulation mode requires time_limit column");
        }
    }
```

## Simulation Logic Updates

### Initialization

```cpp
void Simulation::initialize() {
    // Load trace
    m_trace.load_data(max_num_jobs);
    
    // Check trace mode
    if (m_trace.get_mode() == TraceMode::REPLAY) {
        m_mode = SimulationMode::REPLAY;
        // Jobs already have begin_time/end_time from trace
    } else {
        m_mode = SimulationMode::SCHEDULE;
        // Determine actual_duration for each job
        determine_job_durations();
    }
```

### Duration Determination

```cpp
void Simulation::determine_job_durations() {
    for (auto& job : m_trace.data()) {
        tdiff_t duration;
        
        switch (m_params.m_run_time_mode) {
            case RunTimeMode::ACTUAL:
                // Already read from trace
                duration = job.get_actual_duration();
                break;
                
            case RunTimeMode::LIMIT:
                duration = job.get_time_limit();
                job.set_actual_duration(duration);
                break;
                
            case RunTimeMode::DISTRIBUTION:
                duration = sample_duration(
                    job.get_time_limit(),
                    m_params.m_duration_distribution,
                    m_params.m_duration_scale,
                    m_params.m_duration_stddev
                );
                job.set_actual_duration(duration);
                break;
        }
    }
}

tdiff_t Simulation::sample_duration(
    tdiff_t time_limit,
    DistributionType dist,
    double scale,
    double stddev)
{
    switch (dist) {
        case DistributionType::NORMAL: {
            double mean = time_limit * scale;
            double sd = time_limit * stddev;
            double duration = std::normal_distribution<>(mean, sd)(m_rng);
            return std::max(0.0, duration);
        }
        
        case DistributionType::LOGNORMAL: {
            double mu = std::log(time_limit * scale);
            double sigma = stddev;
            return std::lognormal_distribution<>(mu, sigma)(m_rng);
        }
        
        case DistributionType::UNIFORM: {
            double min_duration = time_limit * scale;
            double max_duration = time_limit * stddev; // reuse param
            return std::uniform_real_distribution<>(min_duration, max_duration)(m_rng);
        }
    }
}
```

### Job Completion

```cpp
void Simulation::schedule_end_event(job_no_t job_idx, sim_time_t start_time) {
    auto& job = m_trace.data()[job_idx];
    
    if (m_mode == SimulationMode::SCHEDULE) {
        // SIMULATION: Use pre-determined actual_duration
        tdiff_t actual_duration = job.get_actual_duration();
        sim_time_t end_time = start_time + actual_duration;
        
        // Update job record with computed times
        job.set_begin_time(to_epoch(start_time));
        job.compute_end_time();
        
        // Schedule completion event
        m_event_queue.emplace(job_idx, to_epoch(end_time), false);
    } else {
        // REPLAY: Job already has end_time from trace
        // (this code path shouldn't be used in replay mode)
    }
}
```

## Command Line Interface

### New Parameters

```cpp
// Duration determination mode
enum class RunTimeMode {
    ACTUAL,   // Read actual_duration from trace
    LIMIT,         // actual_duration = time_limit
    DISTRIBUTION   // Sample from distribution
};

// Distribution type
enum class DistributionType {
    NORMAL,
    LOGNORMAL,
    UNIFORM
};

// In Sim_Params:
RunTimeMode m_run_time_mode;
DistributionType m_run_time_distribution;
double m_run_time_scale;    // Scale factor (e.g., 0.8 = jobs run 80% of estimate)
double m_run_time_stddev;   // Std deviation factor
```

### Example Usage

**Replay mode:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv --total_nodes 100
# Automatically detects replay mode from begin_time column
```

**Simulation with limit mode:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator jobs.csv --total_nodes 100 --run_time_mode limit
# Jobs run exactly as long as time_limit
```

**Simulation with actual run times:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator jobs_with_duration.csv --total_nodes 100 --run_time_mode actual
# Uses actual_run_time column
```

**Simulation with normal distribution:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator jobs.csv --total_nodes 100 \
    --run_time_mode distribution \
    --run_time_distribution normal \
    --run_time_scale 0.8 \
    --run_time_stddev 0.2
# Jobs run ~80% of estimate, with 20% relative std dev
```

**Simulation with lognormal distribution:**
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator jobs.csv --total_nodes 100 \
    --run_time_mode distribution \
    --run_time_distribution lognormal \
    --run_time_scale 0.75 \
    --run_time_stddev 0.5
# Lognormal: median=75% of estimate, shape=0.5
```

## Data Flow

### Replay Mode
```
Trace File (with begin_time, end_time)
    ↓
Parser detects REPLAY mode
    ↓
Job_Record populated with times from trace
    ↓
Simulation replays events at recorded times
    ↓
Output: Same times as input
```

### Simulation Mode
```
Trace File (no begin_time)
    ↓
Parser detects SIMULATION mode
    ↓
Job_Record populated with:
  - submit_time (from trace)
  - time_limit (from trace)
  - actual_duration (from column OR computed)
    ↓
Scheduler computes begin_time for each job
    ↓
Simulation computes end_time = begin_time + actual_duration
    ↓
Job_Record updated with computed times
    ↓
Output: New times based on scheduler decisions
```

## Validation

### Invariants

**In both modes:**
- `end_time >= begin_time`
- `begin_time >= submit_time`
- `actual_duration > 0`

**In simulation mode only:**
- `begin_time` is computed by scheduler, not from trace
- `end_time = begin_time + actual_duration` (no rounding errors)

**In replay mode only:**
- `begin_time` comes from trace
- `end_time` comes from trace OR computed from `begin_time + duration`

### Test Cases

1. **Replay mode with end_time column** - Times match input exactly
2. **Replay mode with duration column** - Computed end_time matches expected
3. **Simulation mode with exact durations** - `actual_duration == time_limit`
4. **Simulation mode with column** - Uses provided actual_duration
5. **Simulation mode with normal distribution** - Mean ≈ time_limit * scale
6. **Simulation mode with lognormal** - Median ≈ time_limit * scale

## Implementation Plan

1. ✅ Create this design document
2. [ ] Update `Job_Record` with new fields and methods
3. [ ] Update `Data_Columns` to detect mode from columns
4. [ ] Add `RunTimeMode` and `DistributionType` enums to `sim_params.hpp`
5. [ ] Implement `determine_job_durations()` in `Simulation`
6. [ ] Add distribution sampling functions
7. [ ] Update `schedule_end_event()` to use actual_duration
8. [ ] Add command-line parsing for new options
9. [ ] Create test traces for each mode
10. [ ] Update Test 0 to validate simulation mode properly
11. [ ] Update documentation

## Benefits

1. **Logical consistency** - No "future knowledge" in simulation
2. **Flexibility** - Multiple run time models for different scenarios
3. **Correctness** - Proper separation of user estimates vs actual
4. **Testability** - Can validate scheduler with controlled run times
5. **Realism** - Can model estimation errors with distributions

## Migration Path

**Existing traces (replay format) continue to work:**
- Parser detects `begin_time` → replay mode
- No changes needed to existing test traces

**New simulation traces:**
- Remove `begin_time` and `end_time` columns
- Add `actual_run_time` OR use `--run_time_mode limit`

**Backwards compatibility:**
- If both `begin_time` and `time_limit` present → replay mode (safe default)
- New `--force-simulation` flag to override if needed
