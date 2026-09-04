# Trace File Format Options

DR_EVT now supports flexible trace file formats with command-line options for format, timestamp style, and timezone.

## Command-Line Options

### Trace Format
```bash
--trace_format {simple|lassen}
```

**simple**: Minimal CSV format for testing
- Format: `[arrival_time, start_time, end_time, num_nodes, exit_status, queue, time_limit]`
- First 4 columns required
- Additional columns optional

**lassen** (default): LLNL Lassen 33-column format
- Full HPC trace format
- Backward compatible with existing traces

### Timestamp Format
```bash
--timestamp_format {epoch|iso}
```

**epoch**: Unix epoch seconds (integers)
- Example: `0`, `100`, `1234567890`
- Fast to parse, no timezone issues
- Best for synthetic test traces

**iso** (default): Human-readable timestamps
- Example: `2024-01-15T10:30:00`, `2024-01-15 10:30:00`
- Requires timezone specification
- Used by real HPC traces

### Timezone
```bash
--timezone TIMEZONE
```

Only used when `--timestamp_format=iso`

Examples:
- `--timezone UTC`
- `--timezone America/New_York`
- `--timezone America/Los_Angeles` (default)
- `--timezone Europe/London`

## Usage Examples

### Simple Test Trace with Epoch Times
```bash
./simulator test_trace.csv \
  --trace_format simple \
  --timestamp_format epoch \
  --total_nodes 100 \
  --backfill_policy easy
```

### Simple Trace with ISO Timestamps
```bash
./simulator test_trace.csv \
  --trace_format simple \
  --timestamp_format iso \
  --timezone UTC \
  --total_nodes 100
```

### Lassen Format (Default)
```bash
./simulator lassen_trace.csv \
  --total_nodes 795
# Uses defaults: lassen format, iso timestamps, America/Los_Angeles timezone
```

## Simple Format CSV Structure

### With Epoch Timestamps
```text
job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit
0,0,100,10,0,batch,100
50,100,150,10,0,batch,50
120,150,230,10,0,batch,80
```

### With ISO Timestamps
```text
job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit
2024-01-15T00:00:00,2024-01-15T00:00:00,2024-01-15T00:01:40,10,0,batch,100
2024-01-15T00:00:50,2024-01-15T00:01:40,2024-01-15T00:02:30,10,0,batch,50
2024-01-15T00:02:00,2024-01-15T00:02:30,2024-01-15T00:03:50,10,0,batch,80
```

## Column Descriptions

### Simple Format Columns

| Position | Name | Description | Required |
|----------|------|-------------|----------|
| 0 | job_submit_time | When job arrives/submits | Yes |
| 1 | begin_time | Historical start time from trace | Yes |
| 2 | end_time | Historical end time from trace | Yes |
| 3 | num_nodes | Number of nodes requested | Yes |
| 4 | exit_status | Job exit code | Optional |
| 5 | queue | Queue name (e.g., "batch") | Optional |
| 6 | time_limit | User-provided time limit (seconds). Accepted column-name aliases: `time_limit`, `timelimit`, `walltime` | Optional |
| 7 | actual_run_time | The job's real, historical run time (seconds); used by `--duration_mode actual` and `--run_time_mode column`. Accepted column-name aliases: `actual_run_time`, `duration`, `actual_duration`, `run_time` | Optional |

**Column-name aliases**: `time_limit` and `actual_run_time` are each detected
under several accepted header names (listed above), so an existing trace
can be reused as-is without editing its header - slow to do by hand on a
large file. Only one alias per column is expected to actually be present
in a given file; if more than one is, the first match in the order listed
wins. This applies to the "simple" format only; the aliases have no effect
on the "lassen" format, which is defined by fixed column position rather
than header name.

**Note**: 
- `begin_time` and `end_time` from trace are used to calculate job duration
- For simulation, the scheduler **computes** actual start time
- Jobs run for the historical duration: `duration = end_time - begin_time`
- See [Replay Mode](../../user-guide/command-line.md) (specifically "Replay Mode (Reproduce Historical Behavior)") for replay vs simulation distinction

### Lassen Format
33-column format specific to LLNL HPC traces. Columns used:
- Column 11: `num_nodes`
- Column 23: `begin_time`
- Column 24: `end_time`
- Column 29: `job_submit_time`
- Column 30: `queue`
- Column 32: `time_limit`

## Implementation Status

✅ **Complete**:
- Command-line option parsing
- Parameter passing through all layers
- Data_Columns updated to store format/timestamp/timezone
- Trace constructor accepts all parameters

⚠️ **Remaining**:
- Actual epoch time parsing in job_io.cpp
- Currently, time parsing delegates to existing `convert_time()` function
- Need to add epoch branch: if timestamp_format == "epoch", parse as integer

## To Complete Epoch Support

The remaining work is in [src/trace/job_io.cpp](https://github.com/llnl/dr_evt/blob/main/src/trace/job_io.cpp) or [src/trace/parse_utils.cpp](https://github.com/llnl/dr_evt/blob/main/src/trace/parse_utils.cpp):

```cpp
epoch_t parse_time(const std::string& time_str, const Data_Columns& dcols) {
    if (dcols.get_timestamp_format() == "epoch") {
        // Parse as Unix epoch seconds
        long seconds = std::stol(time_str);
        return {static_cast<time_t>(seconds), 0.0f};
    } else {
        // Parse as ISO/human-readable timestamp
        return convert_time(time_str);  // Existing function
    }
}
```

## Testing

Once epoch parsing is implemented:

```bash
# Create test trace
cat > test.csv << EOF
job_submit_time,begin_time,end_time,num_nodes,exit_status,queue,time_limit
0,0,100,10,0,batch,100
50,100,150,10,0,batch,50
120,150,230,10,0,batch,80
EOF

# Run test
./simulator test.csv \
  --trace_format simple \
  --timestamp_format epoch \
  --total_nodes 100 \
  --backfill_policy easy \
  --priority_policy fcfs \
  --duration_mode actual
```

Expected output should show:
- Job 0 starts at 0, ends at 100
- Job 1 starts at 100, ends at 150
- Job 2 starts at 150, ends at 230
- Sequential execution (no overlap with 10 nodes each in 100-node system)

## See Also

- [User Guide](../../user-guide/overview.md) - Complete usage guide with trace formats
- [Testing Guide](../../TESTING_GUIDE.md) - Test suite and validation
- [Quick Start](../../getting-started/quickstart.md) - Getting started guide
