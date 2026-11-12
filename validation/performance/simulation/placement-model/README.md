# Standalone placement optimization trials

Four changes were tried independently against `a2d45c7`, then combined. Exact
placement remains the correctness reference; aggregate capacity can produce
different schedules and is not used as an equality oracle.

Median scheduling/query time in milliseconds, one warmup and nine measured runs:

| Trial | EASY, 20K jobs | HYBRID R32/Q32, 20K jobs | Earliest-fit queries | Boolean queries |
| --- | ---: | ---: | ---: | ---: |
| Baseline | 54.58 | 54.78 | 121.31 | 3.365 |
| Immediate fast path / lazy timeline | 1.83 | 1.82 | 121.53 | 3.362 |
| Batch running-allocation seeding | 22.89 | 22.77 | 121.25 | 3.361 |
| Reuse local scratch / transfer selected bitmap | 54.46 | 54.27 | 105.24 | 2.969 |
| Boolean fitting without allocation result | 54.63 | 54.78 | 121.05 | 2.828 |
| Combined | 1.81 | 1.82 | 105.17 | 2.829 |

All four changes are retained. The fast path removes planner setup for this
low-load trace, reducing scheduling time about 97%. Batch seeding replaces
pairwise admission and repeated insertion with a temporary disjointness union
and one sort, reducing scheduling time about 58% independently. Scratch reuse
improves the earliest-fit query workload about 13%; the boolean operation
improves its focused workload about 16%. Changes near 1% are treated as noise.
These percentages are independent comparisons, not additive gains.

## Scope and input

Measurements use Apple Clang 21, `-std=c++20 -O3`, macOS arm64, no LTO, and
disabled observers. They are separate from the earlier GCC/Linux results.
[results.json](results.json) records compiler/platform details, samples, and
decision hashes. Container profiling was attempted but did not execute because
automatic approval review failed; no profiler attribution is claimed.

The shared harness follows the 500-node settings and limit runtimes in
`tests/benchmark_wait_queues.sh`: EASY has one reservation slot and unlimited
queue examination; HYBRID has reservation depth 32 and queue examination depth
32. That script's checked-in default input contains 10K jobs. The 20K input used
here is the existing seeded profile documented in the parent README, reproduced
by `run.py`: seed 2718, four arrivals per second, requests of 1–8 nodes, and
durations of 1–30 seconds. SHA256:
`235c1fa1e259081dc207dea32f9cbecf6c723ac2c914a25b92220f91e4e17508`.

At 500 nodes this input has at most four pending and 76 running jobs, and **zero
future reservations**. The immediate fast-path benefit therefore describes a
lightly loaded workload. This trace does not establish performance under
sustained contention. Its scheduling loop calls selection operations, so the
boolean-only change has no effect there.

`queries.cpp` performs 20K queries against an unchanged 500-node timeline with
63 disjoint seven-node running allocations, releasing at times 1 through 63.
Each query requests 256 nodes for 30 seconds. Earliest-fit queries start at
times 0–3 and require repeated release-endpoint searches; boolean queries cycle
through times 0–63. These isolate query costs and exclude planner construction.

The shared scheduling timer includes resource accounting, timeline setup,
queue traversal, reservations, event handling, and identical decision hashes.
It excludes CSV parsing, initial owning job-record construction, output, and
final audits. This is a validation harness, not whole-simulator wall time.
These historical numbers predate CLI/API integration. Whole-simulator placement
measurements are now available in the
[Dane integrated record](integrated-dane-20261112.json). They confirm the setup
fast path on the low-load trace, but show that repeated exact fit and reservation
queries dominate the existing contended 10K trace. See
[Integrated placement bottlenecks](../../../../docs/dev/PERFORMANCE_ANALYSIS.md#integrated-placement-bottlenecks)
for the whole-process timings, limitations, and optimization targets. The table
above remains a focused historical harness and should not be compared directly
with those end-to-end times.

## Reproduce

From a configured checkout with `build/dr_evt_config.hpp`, Python 3, a C++20
compiler, and `patch`:

```bash
python3 validation/performance/simulation/placement-model/run.py \
  --compiler clang++ --output /tmp/placement-results.json
```

Use `--trace /path/to/jobs.csv` to measure another submission-ordered input.
The generated default has exactly 20K jobs. `--baseline` selects the original
revision, which must match the four experimental patches. The script exports
baseline headers into temporary directories, applies one patch per independent
trial, and builds the combined variant against the working checkout. The
benchmark-only `DR_EVT_PLACEMENT_TRIAL` selects eager, lazy, or batch setup at
compile time; it is not a resource-model option. Trial order rotates between
runs. All variants must produce identical schedule, identity, reservation, and
query hashes before results are written.

## Correctness and ownership

Every independent trial passed the original placement unit tests and all 60
saved native Fluxion cases with both exact-only and coarse-filter timelines.
The combined implementation passes those fixtures and expanded boundary,
invalid-input, observer, borrowed-owner, and failure-atomicity tests.

The immediate path is valid only while no future commitment or outage can
block currently free nodes. The driver switches to exact timeline queries when
it first needs future planning. Job records continue to own live allocations;
the timeline borrows immutable masks until the pass ends. Batch validation
uses only temporary storage, and query scratch lives only through one query.
No persistent side map or second allocation owner is added. Capacity scheduling,
CLI, protobuf, Python bindings, checkpoints, and serialized traces are unchanged.

Validation also passed the five affected GCC 13 CTests and Apple Clang
AddressSanitizer/UndefinedBehaviorSanitizer placement tests. Documentation was
checked from the actual checkout with Sphinx 8.2.3, both without XML and with
the CI Doxygen 1.9.8 XML, plus the configured OpenPBS Doxygen check. The existing
exclusion for the unpublished resource-model plan was retained. This is scoped
validation, not a claim that the entire CI matrix ran.

Local Doxygen 1.18 produces four Sphinx warnings for a pre-existing `BasicTrace`
member-pointer declaration; the CI Doxygen version builds without that issue.

## Whole-simulator scripts

[SIMULATOR.md](SIMULATOR.md) gives cross-platform build, validation and benchmark
commands. `benchmark_placement.py` measures the integrated simulator with
independent output audits and persistent reports. The existing
`tests/benchmark_wait_queues.sh` keeps its original input; generated 20K traces
require an explicit separate invocation.
