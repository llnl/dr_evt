# Input Trace Files

DR_EVT supports multiple input formats, trace data models, timestamp styles,
and timezone handling.

Use `--trace_format` to select the CSV parser and `--trace_type` to select the
job and resource data model. These options are independent. Their syntax and
defaults are in [Command-Line Options](command-line.md#trace-format-options).

The `standard` data model contains scheduling and resource fields. The `pcon`
model adds per-job `avgpcon`, `minpcon`, and `maxpcon` values and corresponding
resource-trace columns.

Timestamps may be Unix epoch seconds or ISO 8601 strings. ISO input and output
use the timezone selected by `--timezone`.

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

### Power-Usage Simulation Mode
```text
job_submit_time,num_nodes,time_limit,avgpcon,minpcon,maxpcon
0,2,3,1.5,2.0,3.0
0,1,1,0.5,1.0,1.5
```

Use this form with `--trace_type pcon`. `q_id` remains optional and defaults
to `1` (`Queue1`). The `avgpcon`, `minpcon`, and `maxpcon` columns are required
and recognized only when `--trace_type pcon` is selected. With the default
`--trace_type standard`, they are treated as unrecognized extra columns and
ignored.

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
| `avgpcon` | Average power usage associated with the job | Required only with `--trace_type pcon`; ignored in standard mode |
| `minpcon` | Minimum power usage associated with the job | Required only with `--trace_type pcon`; ignored in standard mode |
| `maxpcon` | Maximum power usage associated with the job | Required only with `--trace_type pcon`; ignored in standard mode |
| `exit_status` | Output-only compatibility field. The simulator currently writes `0`. | Generated output only |
| `actual_run_time` | The job's real, historical run time (seconds); used by `--run_time_mode actual`. Accepted column-name aliases: `actual_run_time`, `duration`, `actual_duration`, `run_time` | Simulation mode, only with `--run_time_mode actual` |

`time_limit` and `actual_run_time` accept the aliases listed above. If multiple
aliases for one field are present, the first listed match is used. Lassen
input uses fixed column positions instead of header names.

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

For implementation details, see
[Simulation vs Replay Modes](../dev/design-decisions/SIMULATION_VS_REPLAY_MODES.md).

### Lassen Format
33-column format specific to LLNL HPC traces. Columns used:
- Column 11: `num_nodes`
- Column 23: `begin_time`
- Column 24: `end_time`
- Column 29: `job_submit_time`
- Column 30: `queue`
- Column 32: `time_limit`

## See Also

- [User Guide](overview.md) - User guide overview
- [Testing Guide](../TESTING_GUIDE.md) - Test suite and validation
- [Quick Start](../getting-started/quickstart.md) - Getting started guide
- [Fugaku Power-Usage Experiment](fugaku-power-experiment.md) - Large-scale
  power-usage input and progressive loading example
