# Protocol Buffer Configuration

A Protobuf-enabled build can read simulator settings from a text-format
configuration file. Build instructions are in
[Installation](../getting-started/installation.md#cmake-configuration-options),
and option behavior is defined in
[Command-Line Options](command-line.md).

## Fields

For fields with a CLI counterpart, names match the long command-line names
with the leading `--` removed, except that the field corresponding to
`--check_memory_pressure` is named `memory_pressure_fraction`:

| Group | Fields |
|---|---|
| Input and output | `infile`, `infile_list`, `outfile`, `resource_trace`, `redis_uri`, `redis_key_prefix` |
| Limits/system | `max_jobs`, `max_time`, `sim_start_time`, `warm_start_wait_queue`, `total_nodes`, `capacity_schedule` |
| Scheduling | See the [Scheduling controls](#scheduling-controls) table below for every field, accepted values, and defaults. |
| Queue storage | `wait_queue_capacity`, `wait_queue_overflow` |
| Job storage | `job_store_capacity`, `job_store_overflow`, `job_flush_interval`, `memory_pressure_fraction` |
| Resource history | `resource_history_capacity` |
| Checkpoint | `checkpoint_file`, `checkpoint_interval_jobs` |
| Trace handling | `trace_type`, `trace_format`, `cores_per_node`, `timestamp_format`, `timezone`, `msec_output` |
| Runtime model | `run_time_mode`, `run_time_distribution`, `run_time_scale`, `run_time_stddev` |
| Node release | `node_release_delay`, `node_release_delay_model`, `node_release_delay_spread` |
| Other | `seed`, `verbose` |

See [Command-Line Options](command-line.md) for accepted values, defaults, and
interactions. Input schemas are documented in
[Input Trace Files](trace-formats.md), and generated files in
[Output Trace Files](output-traces.md).

`max_time`, when positive, is an inclusive absolute simulation-time boundary:
events at that timestamp are processed and later events are left pending. It
must be greater than or equal to `sim_start_time` when warm start is enabled.

## Scheduling controls

These are fields of `Simulation_Params`, including the resource controls used
by the [supported production configurations](SCHEDULER_LOOPS.md#supported-policy-combinations).
Boolean CLI switches become `true` or `false` fields. Policy names are quoted
strings: `fcfs_1s_evt_group` is a value of `priority_policy`, not a separate
field. Detailed scheduling behavior is defined in
[Command-Line Options](command-line.md).

| Field | Type / default | Accepted values and purpose |
|---|---|---|
| `priority_policy` | String / `"fcfs"` | `"fcfs"`, `"fcfs_alt"`, `"fcfs_conservative"`, `"fcfs_1s_evt_group"`, `"sjf"`, `"ljf"`. The one-second event-group option selects the OpenPBS forecast. |
| `backfill_policy` | String / `"easy"` | `"none"`, `"easy"`, `"hybrid"`, `"conservative"`. Hybrid requires positive reservation depth; ordinary event-driven conservative uses `priority_policy: "fcfs_conservative"`. |
| `queue_impl` | String / `"circular"` | `"circular"`, `"deque"`, `"multimap"`, `"block"`. Periodic and placement-pass modes require circular FCFS queues. |
| `block_size` | Unsigned integer / `128` | `4`, `8`, `16`, `32`, `64`, `128`, `256`; used by the block queue. |
| `reservation_depth` | Unsigned integer / `0` | Protected waiting-job count R for hybrid; must be positive with hybrid. EASY uses R=1 and conservative has no R limit. |
| `backfill_queue_depth` | Unsigned integer / `0` | Examination count Q; zero is unlimited. Placement-pass modes include immediate starts. Keep zero with fixed dispatch. |
| `resource_model_diagnostics` | Boolean / `false` | Select per-simulation resource operation counters at construction, equivalent to `--resource_model_diagnostics`. |
| `per_pass_accounting` | Boolean / `false` | Flux-style event-driven placement passes with fresh Q/R. Supports all four backfill policies. |
| `backfill_interval` | Seconds / `0` | Nonnegative finite interval; positive values select separate periodic backfill with event-driven FCFS dispatch. |
| `dispatch_interval` | Seconds / `0` | Nonnegative finite interval; positive values select aligned combined dispatch/backfill ticks. |
| `dispatch_queue_depth` | Unsigned integer / `0` | Full candidate prefix per fixed-dispatch wake; zero is unlimited. Requires positive dispatch interval. |
| `dispatch_on_events` | Boolean / `false` | Also wake fixed dispatch on arrival batches, preserving tick phase; requires positive dispatch interval. |
| `diagnostic_no_reservations` | Boolean / `false` | Slurm license-off diagnostic; scan current capacity without future reservations. Requires separate periodic hybrid/conservative backfill. |
| `planning_horizon` | Optional seconds / omitted | Nonnegative finite reservation look-ahead H. Omit for unlimited H; explicitly setting zero allows only starts at the current planning time. |
| `planning_time_resolution` | Seconds / `0` | Nonnegative finite grid for projected releases; zero means exact planning. Actual event timestamps remain exact. |
| `num_max_candidates` | Unsigned integer / `1` | Feasible jobs offered to the experimental custom-EASY callback. A field value of zero keeps the default; CLI values must be positive. |
| `total_nodes` | Unsigned integer / `795` | Physical maximum of the homogeneous pool and oversized-job rejection threshold. |
| `capacity_schedule` | String / omitted | CSV capacity change points; values must not exceed `total_nodes`. Reductions do not preempt running jobs. |
| `node_release_delay` | Seconds / `0` | Nonnegative finite per-node cleanup delay after actual completion. Zero disables cleanup unless uniform spread is positive. Replayed history is not delayed. |
| `node_release_delay_model` | String / `"constant"` | `"constant"`, `"exponential"`, `"uniform"`. A job's nodes return when its slowest node finishes cleanup. |
| `node_release_delay_spread` | Seconds / `0` | Nonnegative finite half-width for uniform cleanup; sampled negative delays are floored at zero. Ignored by other models. |

Fixed dispatch and separate periodic backfill cannot both have positive
intervals. Fixed dispatch uses `dispatch_queue_depth`, with
`backfill_queue_depth: 0`. Per-pass accounting and the OpenPBS forecast require
zero intervals, zero dispatch queue depth, no arrival-dispatch notifications,
and an omitted planning horizon. They cannot use the license-off diagnostic.
Hybrid protection requires positive `reservation_depth` in every mode.

Slurm license planning on uses protected periodic backfill with
`diagnostic_no_reservations: false`; DR_EVT has no `bf_licenses` field.
Spur uses fixed dispatch with `dispatch_on_events: true`. The LSF illustration
uses fixed dispatch and EASY protection, assuming aligned sessions; its strict
admission rule requires the CMake option
`-DDR_EVT_LEGACY_RESERVATION_BARRIER=ON`. That build option is not a
configuration field. Full option sets and modeling limits are in
[Scheduling Loops](SCHEDULER_LOOPS.md#supported-policy-combinations).

For example, the Flux-style hybrid configuration is:

```text
priority_policy: "fcfs"
queue_impl: "circular"
backfill_policy: "hybrid"
reservation_depth: 32
backfill_queue_depth: 64
per_pass_accounting: true
planning_time_resolution: 1
```

For OpenPBS one-second forecast event grouping, use:

```text
priority_policy: "fcfs_1s_evt_group"
queue_impl: "circular"
backfill_policy: "hybrid"
reservation_depth: 32
backfill_queue_depth: 0
```

Save either fragment as `scheduler.textproto`, add any input/output or runtime
fields needed by the run, and omit `planning_horizon`. Run with:

```bash
simulator trace.csv --config scheduler.textproto
```

The simulator exposes 53 long CLI options and 51 `Simulation_Params` fields.
Two CLI options have no configuration field: `--help` prints usage and
`--config` selects the configuration file. The other options map to the 51 fields, with
`--check_memory_pressure` renamed to `memory_pressure_fraction`.

`msec_output` is a boolean field, defaulting to `false`. Set it to `true` to
write job and resource timestamps with millisecond precision. Loading a file
where it is `false` or omitted disables any earlier `--msec_output` flag;
a `--msec_output` flag after `--config` enables it again.

## File format

The file contains fields from the `Simulation_Params` message at the top
level:

```text
infile: "trace.csv"
outfile: "results.csv"
resource_trace: "resources.csv"
redis_uri: "redis://127.0.0.1:6379"
redis_key_prefix: "dr_evt:run42"
total_nodes: 1000
sim_start_time: 1713139200
backfill_policy: "easy"
priority_policy: "fcfs"
trace_format: "simple"
timestamp_format: "epoch"
msec_output: true
```

`redis_uri` and `redis_key_prefix` must be set together and require a build
configured with `DR_EVT_WITH_REDIS=ON`. They direct the simulated-job schedule
and resource history to Redis instead of `outfile` and `resource_trace`; see
[Redis Output](redis-output.md).

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
