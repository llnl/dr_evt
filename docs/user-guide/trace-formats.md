# Input Trace Files

DR_EVT supports multiple input formats, trace data models, timestamp styles,
and timezone handling.

## Command-Line Options

### Trace Data Model
```bash
--trace_type {standard|pcon}
```

**standard** (default): Standard DR_EVT job and resource records.

**pcon**: Experimental records carrying per-job `avgpcon`, `minpcon`, and
`maxpcon` values, with corresponding Pcon-aware resource traces.

`--trace_type` is independent of `--trace_format`. For example,
`--trace_type pcon --trace_format simple` uses the simple CSV parser with the
Pcon record model.

### Trace Format
```bash
--trace_format {simple|lassen}
```

**simple** (default): Minimal CSV format for testing
- The parser detects the mode from which columns are present - see [Simulation vs Replay Modes](../dev/design-decisions/SIMULATION_VS_REPLAY_MODES.md) for the full design
- **Simulation mode** (no `begin_time`/`end_time` columns): `job_submit_time, num_nodes, time_limit` required; `q_id` and `actual_run_time` are optional (`actual_run_time` is needed only for `--run_time_mode actual`)
- **Replay mode** (`begin_time` and `end_time` present): `job_submit_time, begin_time, end_time, num_nodes, time_limit` required; `q_id` is optional. Times are historical actuals, replayed exactly, not computed by the scheduler.
- Column order doesn't matter - the parser reads the header row and looks up columns by name

**lassen**: LLNL Lassen 33-column format
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
${CMAKE_INSTALL_PREFIX}/bin/simulator test_trace.csv \
  --trace_format simple \
  --timestamp_format epoch \
  --total_nodes 100 \
  --backfill_policy easy
```

### Simple Trace with ISO Timestamps
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator test_trace.csv \
  --trace_format simple \
  --timestamp_format iso \
  --timezone UTC \
  --total_nodes 100
```

### Lassen Format
```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator lassen_trace.csv \
  --trace_format lassen \
  --total_nodes 795
```

## Simple Format CSV Structure

### Simulation Mode (scheduler computes start/end times)
```text
job_submit_time,num_nodes,time_limit
0,10,100
50,10,50
120,10,80
```

### Replay Mode, With Epoch Timestamps
```text
job_submit_time,begin_time,end_time,num_nodes,time_limit
0,0,100,10,100
50,100,150,10,50
120,150,230,10,80
```

### Replay Mode, With ISO Timestamps
```text
job_submit_time,begin_time,end_time,num_nodes,time_limit
2024-01-15T00:00:00,2024-01-15T00:00:00,2024-01-15T00:01:40,10,100
2024-01-15T00:00:50,2024-01-15T00:01:40,2024-01-15T00:02:30,10,50
2024-01-15T00:02:00,2024-01-15T00:02:30,2024-01-15T00:03:50,10,80
```

### Pcon Simulation Mode
```text
job_submit_time,num_nodes,time_limit,avgpcon,minpcon,maxpcon
0,2,3,1.5,2.0,3.0
0,1,1,0.5,1.0,1.5
```

Use this form with `--trace_type pcon`. `q_id` remains optional and defaults
to `1` (`Queue1`).

## Column Descriptions

### Simple Format Columns

Columns are looked up by name in the header row, not by fixed position -
any order works, and which of `begin_time`/`end_time` are present
determines simulation vs replay mode (see below).

| Name | Description | Required for |
|------|-------------|--------------|
| `job_submit_time` | When the job arrives/submits | Both modes |
| `num_nodes` | Number of nodes requested | Both modes |
| `q_id` | Optional one-based queue ID. If absent, the job uses `1` (`Queue1`). | Both modes |
| `time_limit` | User-provided time limit (seconds). Accepted column-name aliases: `time_limit`, `timelimit`, `walltime` | Both modes |
| `begin_time` | Historical start time from trace | Replay mode only; must appear together with `end_time` |
| `end_time` | Historical end time from trace | Replay mode only; must appear together with `begin_time` |
| `duration` | Accepted alias for `actual_run_time` | Simulation mode, only with `--run_time_mode actual` |
| `avgpcon` | Average Pcon value associated with the job | Pcon trace type |
| `minpcon` | Minimum Pcon value associated with the job | Pcon trace type |
| `maxpcon` | Maximum Pcon value associated with the job | Pcon trace type |
| `exit_status` | Output-only compatibility field. The simulator currently writes `0`. | Generated output only |
| `actual_run_time` | The job's real, historical run time (seconds); used by `--run_time_mode actual`. Accepted column-name aliases: `actual_run_time`, `duration`, `actual_duration`, `run_time` | Simulation mode, only with `--run_time_mode actual` |

**Column-name aliases**: `time_limit` and `actual_run_time` are each detected
under several accepted header names (listed above), so an existing trace
can be reused as-is without editing its header - slow to do by hand on a
large file. Only one alias per column is expected to actually be present
in a given file; if more than one is, the first match in the order listed
wins. This applies to the "simple" format only; the "lassen" format is
defined by fixed column position rather than header name (see below).

**Ignored input columns**: input fields not used by the selected trace format
are ignored. In particular, `exit_status` is accepted only so a generated
simulator output can be used as replay input; its value is never read or used
to affect scheduling or replay. In the default ID-input build, `queue` is
ignored; `q_id` is optional and defaults to `1` (`Queue1`). Legacy named
`queue` input is available only with `-DDR_EVT_LEGACY_QUEUE_INPUT=ON`; in
that build `q_id` is ignored and an absent `queue` also defaults to `Queue1`.

**TODO — user-defined queue names:** Allow users to define the accepted input
queue names and preserve those names in output.

**Mode detection - simulation vs replay**:
- No `begin_time`/`end_time` columns present → **simulation mode**: the
  scheduler computes start times; how the job's actual run time is
  determined is controlled separately by `--run_time_mode`
- `begin_time` and `end_time` present → **replay mode**: times are historical
  actuals, replayed exactly
- Exactly one of `begin_time` or `end_time` present is rejected as an
  ambiguous trace format

See [Simulation vs Replay Modes](../dev/design-decisions/SIMULATION_VS_REPLAY_MODES.md)
for the full design rationale.

### Lassen Format
33-column format specific to LLNL HPC traces. Columns used:
- Column 11: `num_nodes`
- Column 23: `begin_time`
- Column 24: `end_time`
- Column 29: `job_submit_time`
- Column 30: `queue`
- Column 32: `time_limit`

## Testing

```bash
# Create test trace
cat > test.csv << EOF
job_submit_time,begin_time,end_time,num_nodes,time_limit
0,0,100,10,100
50,100,150,10,50
120,150,230,10,80
EOF

# Run test
${CMAKE_INSTALL_PREFIX}/bin/simulator test.csv \
  --trace_format simple \
  --timestamp_format epoch \
  --total_nodes 100 \
  --backfill_policy easy \
  --priority_policy fcfs
```

Expected output should show:
- Job 0 starts at 0, ends at 100
- Job 1 starts at 100, ends at 150
- Job 2 starts at 150, ends at 230
- Sequential execution (no overlap with 10 nodes each in 100-node system)

## See Also

- [User Guide](overview.md) - Complete usage guide with trace formats
- [Testing Guide](../TESTING_GUIDE.md) - Test suite and validation
- [Quick Start](../getting-started/quickstart.md) - Getting started guide
