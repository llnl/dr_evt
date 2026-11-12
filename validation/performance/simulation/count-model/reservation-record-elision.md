# Reservation-record elision for unlimited Q

The per-pass scheduler now uses a scalar reservation count to enforce R and
identify the first reservation. It records `(job, start, duration)` tuples only
when Q is bounded, because only that path can retry and compare reservation
plans. Prediction records and the bounded-Q retry guard retain their behavior.

The measured baseline is the standalone CountModel before this optimization,
not the original pre-extraction scheduler. Its binary hash matches the
CountModel candidate in the first milestone reports. New reports include hashes
of the changed source files and identify the previous scheduler source hash.

## Allocation-focused case

`benchmark_reservation_records.cpp` sets up 20,000 pending, full-machine jobs
behind one full-machine runner. Conservative backfill reserves each pending
job for a future one-second interval. Q is unlimited. The benchmark validates
every reservation prediction and checks that all jobs remain pending.

It replaces ordinary `operator new/delete` in the benchmark executable to count
calls and requested bytes during the scheduling traversal. Queue setup,
prediction-cache warmup, validation, and output are excluded. After warmup, the
prediction cache reuses its storage, making the discarded tuple vector's cost
visible. This instrumentation is not linked into the normal simulator.

| Per scheduling call | Before | After |
| --- | ---: | ---: |
| Ordinary allocation calls | 19 | 3 |
| Requested heap bytes | 1,572,952 | 112 |
| Median traversal time | 0.741 ms | 0.190 ms |

All nine samples had the same allocation counts and requested bytes. The
allocation-focused traversal took about 74% less time. Timing samples ran as
one baseline batch followed by one candidate batch, so their elapsed times are
illustrative; the allocation reduction is deterministic. Requested bytes are
cumulative allocation sizes per call, not peak memory usage.

## Whole-simulation comparison

The existing driver compares the old CountModel binary, the optimized binary,
and the optimized binary with diagnostic hooks. It rotates execution order,
checks exact schedule/resource-history hashes for every execution, and audits
job counts, durations, start ordering, and capacity.

Four 2,000-job bounded-Q cases used nine measured samples of 20 executions.
The initially noisy EASY case was repeated with 15 samples of 40 executions.
The 8,000-job unlimited-Q conservative case used one warmup and nine measured
runs. Builds use GCC 13.3.0, Linux arm64 Docker on an Apple M3 Max, Release
(`-O3 -g`), static DR_EVT libraries, no LTO, and optional dependencies disabled.

| Case | Candidate / baseline paired median change | Bootstrap 95% interval |
| --- | ---: | --- |
| Bounded Q, no backfill | -0.11% | -0.15% to +0.50% |
| Bounded Q, EASY (recheck) | +0.32% | -0.70% to +1.49% |
| Bounded Q, HYBRID | -0.28% | -0.71% to +1.00% |
| Bounded Q, conservative | -0.05% | -0.32% to +0.98% |
| Unlimited Q, conservative stress | -0.16% | -0.21% to +0.08% |

The mixed whole-simulation workload shows no measurable speedup or regression.
Keep the change for its lower allocation volume and clear benefit in the
reservation-only case; it does not address the dominant interval-search cost
in the mixed workload. Other proposed data-structure changes remain separate
experiments.

## Correctness

- Added a native unlimited-Q test for EASY, HYBRID, and conservative policies.
  It checks R, prediction results, immediate backfill, pending jobs, and the
  single-pass behavior when Q is unlimited.
- Placement-pass, count-model oracle/hooks, OpenPBS forecast, capacity,
  release-delay, and prediction native checks passed with hooks disabled and
  enabled. Placement-pass integration checks also passed in both builds.
- The 34 scheduler golden fixtures and 37 release-delay checks passed.
- Every whole-simulation measurement matched the baseline's schedule and
  aggregate resource-history hashes exactly.

## Evidence and reproduction

- [Allocation measurements](tuple-allocation-comparison.json)
- [Bounded-Q measurements](tuple-bounded-comparison.json)
- [EASY recheck](tuple-easy-recheck.json)
- [Unlimited-Q stress measurements](tuple-stress-comparison.json)
- [Whole-simulation driver instructions](../README.md#standalone-count-model-comparison)

For this comparison, pass the previous CountModel simulator as `--baseline`,
the optimized simulator as `--model`, and its diagnostic build as `--hooks`.
The reused driver's `abstraction_wall` and `abstraction_cpu` fields represent
this optimization's candidate/baseline change in these reports.

Compile the allocation benchmark against each build separately. The following
link dependencies are those used by the Linux builds in this experiment:

```bash
benchmark_build=/tmp/count-before-build
benchmark_source=/tmp/count-before-source
g++ -std=c++20 -O3 -DNDEBUG -fPIC -g \
  -I "$benchmark_build" -I "$benchmark_source/src" \
  validation/performance/simulation/benchmark_reservation_records.cpp \
  "$benchmark_build/libdr_evt.a" \
  -lboost_program_options -lboost_serialization -ldl \
  -o /tmp/reservation-records-before
/tmp/reservation-records-before 20000 9
```

Repeat with the candidate build/source and a separate output executable. Use
matching toolchain and build options for both libraries and benchmark programs.
