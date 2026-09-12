# DR_EVT Test Suite

This page is the source of truth for test categories, runner commands, and
test counts. The [Testing Guide](../docs/TESTING_GUIDE.md) explains the
validation methodology.

## Build and run

Configure and build before invoking the shell runners:

```bash
cmake -S . -B build
cmake --build build -j4
```

Run the ordinary CTest tests enabled by the current configuration with:

```bash
ctest --test-dir build --output-on-failure
```

The focused runners below exercise fixture suites and comparisons that are not
all registered with CTest:

```bash
./tests/run_scheduler_correctness_tests.sh
./tests/run_unit_tests.sh
./tests/run_feature_tests.sh
./tests/run_scale_tests.sh
./tests/run_easy_vs_conservative_correctness_tests.sh
./tests/compare_cpp_python_conservative.sh
./tests/run_replay_tests.sh
./tests/run_resource_history_tests.sh
./tests/run_job_store_tests.sh
./tests/run_append_job_tests.sh
./tests/run_progressive_load_tests.sh
./tests/run_configs_tests.sh
```

Some runners require build options or external packages for Python bindings,
Protobuf, gRPC, or MPI. Tests for unavailable optional features are not built
or are reported as skipped.

## Inventory

| Category | Count | Runner or registration | Coverage |
|---|---:|---|---|
| Scheduler correctness | 34 | `run_scheduler_correctness_tests.sh` | C++/Python schedule and resource-trace consistency |
| Unit | 7 | `run_unit_tests.sh` | Basic parsing, formats, and execution |
| Feature | 6 | `run_feature_tests.sh` | Policies, modes, rejection, and output formats |
| Scale | 7 | `run_scale_tests.sh` | Workloads from 10 to 2,000 jobs |
| Conservative backfilling | 2 | two conservative runners above | Behavioral and C++/Python comparisons |
| Replay | 5 | `run_replay_tests.sh` | Resource equivalence and reclamation safety |
| Resource history | 5 | `run_resource_history_tests.sh` | Circular-buffer output and capacity handling |
| Job store | 6 | `run_job_store_tests.sh` | Capacity, growth/abort, reclamation, and statistics |
| Append-job | 18 | `run_append_job_tests.sh` | Streaming insertion and advancement |
| Progressive loading | 14 | `run_progressive_load_tests.sh` | Multi-file loading, bounded storage, and memory checks |
| Protobuf configuration | 9 | `run_configs_tests.sh` | Configuration/CLI parity and documented examples |
| Core CTest | 4 | CTest | RNG generation/state restoration, power trace storage, and CLI dispatch |
| **Total** | **117** | | |

The gRPC portion of the append-job runner is skipped when gRPC support was not
built. Additional gRPC, MPI, Python, queue-differential, column-alias, and
run-time-mode runners live in this directory and are used when their features
or focused coverage are needed.

## Fixture locations

- `test_traces/scheduler_correctness/`: small FCFS/EASY comparison fixtures;
- `test_traces/unit/`: parsing and basic execution fixtures;
- `test_traces/feature/`: policy, replay, and buffer fixtures;
- `test_traces/scale/`: larger workloads; and
- `test_configs/`: Protobuf configuration fixtures.

The scheduler-correctness layout is described in its
[fixture README](test_traces/scheduler_correctness/README.md). Input and output
schemas are defined in [Input Trace Files](../docs/user-guide/trace-formats.md)
and [Output Trace Files](../docs/user-guide/output-traces.md).

## Notable suites

### Scheduler-correctness tests

Scheduler-correctness fixtures compare both the scheduled-job trace and the
resource trace with outputs from `scripts/python_reference_scheduler.py`.
These comparisons test consistency between implementations, not independent
proof of the algorithm.

### Replay tests

Replay tests contain four simulation/replay CLI comparisons and the
`test_replay_reclamation` binary. The latter covers explicit, periodic, and
capacity-triggered flushing; final output; equal-time departures;
out-of-order completion; front-prefix blocking; and exactly-once output.

### Progressive-loading tests

Progressive-loading tests exercise `--infile_list`, `Trace::load_next_file()`,
and `Simulation::run_progressive()`. Buffer behavior is documented in
[Output-Trace Buffers](../docs/dev/OUTPUT_TRACE_BUFFERS.md), and streaming API
semantics in the [C++ Streaming API](../docs/api/STREAMING_API.md).

### Wait-queue tests

```bash
./tests/run_fcfs_queue_implementation_tests.sh --correctness
./tests/benchmark_block_sizes.sh
${CMAKE_INSTALL_PREFIX}/bin/tests/test_block_queue
```

The [queue implementation runner](run_fcfs_queue_implementation_tests.sh)
compares the
scheduled-job and resource traces produced by the `circular`, `deque`,
`multimap`, and `block` FCFS queues. [`test_block_queue.cpp`](test_block_queue.cpp)
directly exercises the block queue at every supported block size.
[`benchmark_block_sizes.sh`](benchmark_block_sizes.sh)
runs the end-to-end performance and output-equivalence benchmark documented in
[Wait Queues](../docs/dev/WAIT_QUEUES.md#benchmark-record). Its workload is
[`test_traces/scale/huge_10000jobs.csv`](test_traces/scale/huge_10000jobs.csv);
the differential runner uses the
[`scheduler_correctness`](test_traces/scheduler_correctness/) fixtures.

### Streaming API tests

```bash
./tests/run_append_job_tests.sh
./tests/run_progressive_load_tests.sh
${CMAKE_INSTALL_PREFIX}/bin/tests/test_batch_vs_streaming
```

The [append-job runner](run_append_job_tests.sh) provides direct C++ and gRPC
`append_job()` coverage.
[`test_batch_vs_streaming.cpp`](test_batch_vs_streaming.cpp) compares batch and
incremental execution, while
[`run_progressive_load_tests.sh`](run_progressive_load_tests.sh) covers the
separate progressive-file input path. The runnable Python streaming example is
[`python/example_streaming.py`](../python/example_streaming.py).

### Python API tests

After building with `DR_EVT_BUILD_PYTHON=ON`, run:

```bash
./tests/run_python_tests.sh
```

[`test_python_api.py`](test_python_api.py) exercises
configuration, single and batched job append,
time advancement, statistics, output, and error handling.

### Distributed client/server tests

After building with `DR_EVT_ENABLE_GRPC=ON`, run:

```bash
./tests/run_grpc_tests.sh
```

The [gRPC runner](run_grpc_tests.sh) covers
the example client/server pair and, when MPI is available, the multi-client,
multi-server harness. [`run_append_job_tests.sh`](run_append_job_tests.sh) also
tests `AppendJobRequest` over an actual gRPC connection, and
[`test_grpc_single_coordinator.py`](test_grpc_single_coordinator.py) covers the
synchronized independent-systems use case. The related fixtures are in
[`test_traces/grpc/`](test_traces/grpc/).

## Adding tests

Add a fixture to the directory for the behavior it covers and register it in
the corresponding runner. Keep temporary and generated output outside the
repository root.

For a scheduler-correctness fixture:

1. Add `test_traces/scheduler_correctness/<name>.csv`.
2. Add the name to `scripts/generators/generate_all_expected_outputs.py`.
3. Generate and review `<name>.expected_output.csv` and, when applicable,
   `<name>.expected_resources.csv`.
4. Add a small hand-derived `.construction.md` or `.answer.json` when direct
   inspection provides independent confidence.
5. Run `run_scheduler_correctness_tests.sh` and any other affected suite.

Regenerate expected outputs only for an intentional behavior change, and
review the generated diff before committing it.
