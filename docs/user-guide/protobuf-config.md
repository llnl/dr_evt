# Protocol Buffer Configuration

A Protobuf-enabled build can read simulator settings from a text-format
configuration file. Build instructions are in
[Installation](../getting-started/installation.md#cmake-configuration-options),
and option behavior is defined in
[Command-Line Options](command-line.md).

## Fields

Configuration field names match the long command-line names with the leading
`--` removed, except that the configuration field corresponding to
`--check_memory_pressure` is named `memory_pressure_fraction`:

| Group | Fields |
|---|---|
| Input and output | `infile`, `infile_list`, `outfile`, `resource_trace` |
| Limits | `max_jobs`, `max_time`, `total_nodes` |
| Scheduling | `backfill_policy`, `priority_policy`, `queue_impl`, `block_size` |
| Queue storage | `wait_queue_capacity`, `wait_queue_overflow` |
| Job storage | `job_store_capacity`, `job_store_overflow`, `job_flush_interval`, `memory_pressure_fraction` |
| Resource history | `resource_history_capacity` |
| Trace handling | `trace_type`, `trace_format`, `timestamp_format`, `timezone` |
| Runtime model | `run_time_mode`, `run_time_distribution`, `run_time_scale`, `run_time_stddev` |
| Other | `seed`, `verbose` |

See [Command-Line Options](command-line.md) for accepted values, defaults, and
interactions. Input schemas are documented in
[Input Trace Files](trace-formats.md), and generated files in
[Output Trace Files](output-traces.md).

## File format

The file contains fields from the `Simulation_Params` message at the top
level:

```text
infile: "trace.csv"
outfile: "results.csv"
resource_trace: "resources.csv"
total_nodes: 1000
backfill_policy: "easy"
priority_policy: "fcfs"
trace_format: "simple"
timestamp_format: "epoch"
```

Do not wrap the fields in `simulation_params { ... }`. The authoritative
schema is
[`src/proto/dr_evt_params.proto`](https://github.com/LLNL/dr_evt/blob/main/src/proto/dr_evt_params.proto).

## Running with a configuration

For single-file input, the simulator still requires the positional trace path.
It takes precedence over the `infile` field:

```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv \
  --config sim_config.textproto
```

For progressive input, set `infile_list` in the configuration and omit the
positional path:

```text
infile_list: "traces/file_list.txt"
job_store_capacity: 50000
```

```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator --config progressive.textproto
```

Options are applied from left to right. Arguments after `--config` override
the configuration file:

```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator trace.csv \
  --config sim_config.textproto \
  --total_nodes 2000
```

## Validation

Unknown fields, invalid value types, unsupported option values, and conflicting
input selections are rejected with an error.

The documented examples and CLI/config equivalence are exercised by
[`tests/test_protobuf_config_doc_examples.py`](https://github.com/LLNL/dr_evt/blob/main/tests/test_protobuf_config_doc_examples.py)
and
[`tests/run_configs_tests.sh`](https://github.com/LLNL/dr_evt/blob/main/tests/run_configs_tests.sh).

## See also

- [Command-Line Options](command-line.md)
- [Input Trace Files](trace-formats.md)
- [Output Trace Files](output-traces.md)
