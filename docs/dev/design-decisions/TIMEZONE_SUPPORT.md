# Timezone Support

## Status: Partially Implemented

DR_EVT supports a configured IANA/POSIX timezone for ISO timestamps that do
not contain an offset. Numeric offsets embedded in timestamps are recognized,
but that path still has correctness and integration work described below.

## Current User-Facing Behavior

The `--timezone` option supplies the timezone used to interpret ISO timestamps
without an embedded offset:

```bash
simulator trace.csv \
  --timestamp_format iso \
  --timezone America/Los_Angeles
```

The same setting is available as `timezone` in Protobuf configuration and in
the gRPC simulation request. Internally, `Data_Columns` temporarily sets the
process `TZ` value while parsing a trace and restores the previous value when
the mapping is destroyed.

The supported user-facing behavior is documented in:

- [Trace Formats](../../user-guide/trace-formats.md)
- [Command-Line Options](../../user-guide/command-line.md)
- [Protobuf Configuration](../../user-guide/protobuf-config.md)

## Embedded Numeric Offsets

`parse_time_with_timezone()` recognizes ISO timestamps ending in `Z` or a
numeric offset such as `-08:00`. It returns an epoch value and the extracted
offset. `to_local_time_string()` performs the inverse fixed-offset formatting
operation.

This path is not complete:

- `parse_time_with_timezone()` currently calls `std::mktime()`, which applies
  the process timezone, and then subtracts the embedded numeric offset. When
  the configured timezone is not UTC, the conversion can apply both offsets.
- `set_by(epoch_t&, ...)` discards the extracted offset after parsing, so the
  original offset is not retained with the job.
- `Trace` exposes default and per-queue offset metadata, but parsing does not
  populate that metadata.
- Simulation output does not use `to_local_time_string()` or the stored
  timezone metadata.

Consequently, embedded-offset parsing must not yet be described as complete
UTC normalization with round-trip local-time output.

## Existing Coverage

[`tests/test_traces/unit/timezone_handling.csv`](https://github.com/LLNL/dr_evt/blob/main/tests/test_traces/unit/timezone_handling.csv)
exercises three timestamps with
different numeric offsets through the unit-test runner. Its expected result
captures the current behavior, but there are no direct unit tests for
`parse_time_with_timezone()`, invalid offsets, fractional seconds, or
`to_local_time_string()`.

## Remaining Work

1. Convert embedded-offset timestamps independently of the process timezone.
2. Validate the full offset syntax and range before conversion.
3. Decide whether original offsets are retained per job, per queue, or only
   used during input normalization.
4. Integrate local-time formatting with output if round-trip display remains a
   requirement; otherwise remove the unused metadata and formatter.
5. Add direct tests and correct the timezone fixture's UTC expectations.
6. Update the user guide once the supported embedded-offset behavior is fixed
   and tested.
