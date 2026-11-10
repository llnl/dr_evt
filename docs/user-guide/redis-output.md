# Redis Output

DR_EVT can write the simulated-job schedule and resource history to Redis
instead of CSV files. Redis output is optional and disabled in normal builds.
The Redis representation keeps both complete CSV documents while also indexing
jobs by ID, submission time, start time, completion time, and requested node
count.

## Install Redis and hiredis

Redis++ is downloaded automatically during CMake configuration, but it requires
the hiredis client library. A Redis server is also required at runtime.

Ubuntu or Debian:

```bash
sudo apt update
sudo apt install redis-server libhiredis-dev
```

RHEL-compatible systems:

```bash
sudo dnf install redis hiredis-devel
```

macOS with Homebrew:

```bash
brew install redis hiredis
```

Package names and service-management commands can differ between releases.

## Build DR_EVT with Redis support

```bash
cmake -S . -B build -DDR_EVT_WITH_REDIS=ON
cmake --build build -j
```

CMake first looks for an installed Redis++ package. If none is found, it fetches
the pinned Redis++ release. Builds without `DR_EVT_WITH_REDIS` do not download,
compile, or link Redis++.

## Start and test the Redis server

For a local foreground server:

```bash
redis-server --bind 127.0.0.1 --port 6379
```

Keep that terminal open. From another terminal, confirm connectivity:

```bash
redis-cli -u redis://127.0.0.1:6379 PING
```

The expected response is `PONG`. A system package may alternatively manage the
server with `systemctl` or another service manager.

## Write simulation output to Redis

Supply both the Redis connection URI and a key prefix:

```bash
build/simulator traces/jobs.csv \
  --redis_uri redis://127.0.0.1:6379 \
  --redis_key_prefix dr_evt:run42 \
  --job_flush_interval 1
```

When `--redis_uri` is set, both the simulated-job schedule and resource trace
go to Redis. Neither `--outfile` nor `--resource_trace` is opened. Both Redis
options are required together. Reusing a prefix starts a fresh output and
removes job hashes, indexes, and resource history recorded by the previous run
under that prefix.

`--job_flush_interval 1` attempts a Redis write after every processed job
departure. Output remains in permanent job-ID order: a later backfilled job
that completes early waits until every earlier job is also complete. The
default interval `0` follows the job-store capacity. Capacity pressure, an
explicit [`flush_completed_jobs()`][cpp-simulation-flush-completed-jobs], and final output also trigger writes.

## Key layout

For prefix `dr_evt:run42`, DR_EVT creates:

| Key | Redis type | Contents |
|---|---|---|
| `dr_evt:run42:csv` | string | Complete simulated-job CSV |
| `dr_evt:run42:resources:csv` | string | Complete resource-history CSV |
| `dr_evt:run42:job:<id>` | hash | Searchable fields for one job |
| `dr_evt:run42:job_ids` | set | IDs written by this run |
| `dr_evt:run42:by_submit` | sorted set | Job IDs scored by submission time |
| `dr_evt:run42:by_start` | sorted set | Job IDs scored by start time |
| `dr_evt:run42:by_completion` | sorted set | Job IDs scored by completion time |
| `dr_evt:run42:by_resources` | sorted set | Job IDs scored by `num_nodes` |
| `dr_evt:run42:resource:<sequence>` | hash | Searchable fields for one resource sample |
| `dr_evt:run42:resources:by_time` | sorted set | Resource sequence IDs scored by sample time |

Each job hash contains `job_id`, `job_submit_time`, `begin_time`, `end_time`,
`num_nodes`, `exit_status`, `time_limit`, and `q_id` or `queue` when that input
column exists. Redis consumes the finalized range directly from the existing
trace buffer; there is no sidecar job-record collection. The range's CSV
append, hash fields, ID set, and sorted indexes are updated in one Redis
transaction before the trace records are reclaimed.

Each resource hash contains `sample_id`, `time`, `free_nodes`, and
`allocated_nodes`. Pcon traces also contain `avgpcon`, `minpcon`, and
`maxpcon`. Sequence IDs, rather than timestamps, identify samples because
multiple resource changes can occur at the same time. Redis consumes the
existing resource-history iterator range directly; its CSV append, hashes, and
time index are updated together for each flushed resource block.

DR_EVT keeps job and resource values in their native C++ types until the Redis
serialization boundary. Redis hash fields and collection members are byte
strings by protocol, so `HGET` returns their textual representation. Sorted-set
scores receive numeric values directly rather than reparsing serialized text.
The two `*:csv` strings are retained as compatibility exports; structured
queries should use the hashes and sorted sets.

## Query the output

Read the complete CSV:

```bash
redis-cli -u redis://127.0.0.1:6379 --raw GET dr_evt:run42:csv
```

Read the complete resource-history CSV:

```bash
redis-cli -u redis://127.0.0.1:6379 --raw GET dr_evt:run42:resources:csv
```

Find resource samples from time 100 through 200, including multiple samples
at the same timestamp:

```bash
redis-cli -u redis://127.0.0.1:6379 \
  ZRANGEBYSCORE dr_evt:run42:resources:by_time 100 200
```

Read one returned resource sample:

```bash
redis-cli -u redis://127.0.0.1:6379 \
  HGETALL dr_evt:run42:resource:42
```

Look up job ID 42:

```bash
redis-cli -u redis://127.0.0.1:6379 HGETALL dr_evt:run42:job:42
```

Find job IDs submitted from time 100 through 200, inclusive:

```bash
redis-cli -u redis://127.0.0.1:6379 \
  ZRANGEBYSCORE dr_evt:run42:by_submit 100 200
```

The same range form works for start and completion time:

```bash
redis-cli -u redis://127.0.0.1:6379 \
  ZRANGEBYSCORE dr_evt:run42:by_start 100 200
redis-cli -u redis://127.0.0.1:6379 \
  ZRANGEBYSCORE dr_evt:run42:by_completion 100 200
```

Find jobs requesting exactly 64 nodes:

```bash
redis-cli -u redis://127.0.0.1:6379 \
  ZRANGEBYSCORE dr_evt:run42:by_resources 64 64
```

Find jobs requesting 64 through 256 nodes:

```bash
redis-cli -u redis://127.0.0.1:6379 \
  ZRANGEBYSCORE dr_evt:run42:by_resources 64 256
```

Sorted-set queries return job IDs. Use `HGETALL` with a returned ID to retrieve
the full record.

## Query job status (Redis optional)

Job-status queries do not require Redis. The in-process, Python, and gRPC
[`get_job_statuses()`][cpp-simulation-get-job-statuses] APIs query DR_EVT's in-memory status metadata and work in
ordinary builds as well as Redis-enabled builds. They report pending, running,
completed, and rejected appended jobs; pending jobs can include an estimated
start time.

Redis contains only finalized jobs that have reached an output flush. Pending,
running, and completed-but-not-yet-flushed jobs are deliberately absent so
Redis operations never enter the scheduler's append, start, or completion
paths.

The installed `dr_evt_client` demonstrates an optional Redis-first lookup that
avoids asking the live server about jobs already finalized in Redis. Build the
client and server with both gRPC and Redis enabled to run this example:

```bash
cmake -S . -B build \
  -DDR_EVT_ENABLE_GRPC=ON \
  -DDR_EVT_WITH_REDIS=ON
cmake --build build --target dr_evt_server-bin dr_evt_client-bin -j
```

Start `dr_evt_server`, then run the client with a queue-free simple CSV:

```bash
build/dr_evt_server 127.0.0.1:50051
```

In another terminal:

```bash
build/dr_evt_client 127.0.0.1:50051 traces/jobs.csv \
  --redis-uri redis://127.0.0.1:6379 \
  --redis-key-prefix dr_evt:client-example \
  --advance-to 100
```

The client sets `job_flush_interval` to 1 and performs this concrete sequence:

1. Send every CSV row in one `AppendJobsRequest` and retain the returned IDs.
2. Send one `AdvanceToRequest` for the requested simulation time.
3. Pipeline one Redis `HGETALL` command per returned ID and execute the whole
   pipeline in one network round trip.
4. Send one `GetJobStatusesRequest` containing only IDs whose Redis hashes are
   absent.
5. Print Redis records and live server statuses in original submission order.

For example, a finalized job and a still-running job are reported as:

```text
=== Job Statuses ===
Job 0: completed (Redis, start=0, end=100)
Job 1: running (server, start=100, end=120)
```

A hash found in Redis is the authoritative finalized record. An absent hash
means the job may still be pending, running, completed but waiting for an
ordered flush, or rejected, so only missing IDs are sent back to the server.

[`get_job_statuses()`][cpp-simulation-get-job-statuses] accepts multiple IDs and returns results in request order.
The same bulk status operation is exposed by the Python binding and the gRPC
`GetJobStatusesRequest` API. The no-option client invocation remains available;
it batch-appends the CSV and finishes the simulation without an intermediate
status query. `--advance-to` without Redis queries every appended ID directly
from the server and reports those statuses before finishing.

The query is read-only. Its compact retained status metadata is included in
the checkpoint state, so status queries continue to work after restoring a
checkpoint even when the corresponding trace record has been reclaimed.

## Checkpoint/restart and output rollback

See [Checkpoint/Restart](checkpoint-restart.md#output-rollback-and-recovery)
for file and Redis archive handling, output rollback boundaries, and the
commands that reconstruct output after a resumed run.

## Connection URIs

Redis++ accepts URIs such as:

```text
redis://127.0.0.1:6379
redis://username:password@redis.example.org:6379/2
```

The optional final path selects the Redis database number. Avoid placing
password-bearing commands in shared shell history or process listings. TLS is
not enabled by the current DR_EVT Redis++ configuration.

[cpp-simulation-flush-completed-jobs]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation20flush_completed_jobsEv
[cpp-simulation-get-job-statuses]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation16get_job_statusesERKNSt6vectorI8job_no_tEE6size_tbb
