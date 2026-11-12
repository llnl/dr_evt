# Current simulator measurements

`performance.json` summarizes the six audited validation-workload timings;
`profile.json` summarizes the seeded fixed conservative profile. Both use
revision `f680afd`. Raw profiles and generated outputs stay in local run
folders. [Performance Analysis](../../../docs/dev/PERFORMANCE_ANALYSIS.md)
explains the results and the validation-workload timing command.

## Reproduce the fixed conservative profile

Generate the 20,000-job input from Python's random seed 2718:

```bash
python3 - <<'PY'
import csv, random
rng = random.Random(2718)
with open('/tmp/dr_evt_profile_jobs.csv', 'w') as stream:
    rows = csv.writer(stream, lineterminator='\n')
    rows.writerow(['job_submit_time', 'num_nodes', 'time_limit'])
    rows.writerows((i // 4, rng.randint(1, 8), rng.randint(1, 30))
                  for i in range(20000))
PY
cmake -S . -B build-gprof -DCMAKE_BUILD_TYPE=Release \
  -DDR_EVT_GPROF=ON -DDR_EVT_LEGACY_RESERVATION_BARRIER=OFF \
  -DDR_EVT_WITH_SER20=OFF -DDR_EVT_WITH_UNIT_TESTING=OFF \
  -DDR_EVT_BUILD_PYTHON=OFF \
  -DDR_EVT_ENABLE_PROTOBUF=OFF -DDR_EVT_ENABLE_GRPC=OFF
cmake --build build-gprof --target sim-bin -j2
```

Run from a separate directory because gprof writes `gmon.out`. Include local
and weak C++ symbols when producing the report, so compiler-generated timeline
functions are not charged to an unrelated preceding symbol:

```bash
profile_binary="$PWD/build-gprof/simulator"
mkdir -p /tmp/dr_evt_profile_run
cd /tmp/dr_evt_profile_run
"$profile_binary" /tmp/dr_evt_profile_jobs.csv --total_nodes 128 \
  --trace_format simple --timestamp_format epoch --run_time_mode limit \
  --max_jobs 20000 --queue_impl circular --dispatch_interval 1 \
  --dispatch_queue_depth 10000 --backfill_policy conservative \
  --outfile schedule.csv
nm -n --defined-only "$profile_binary" | sed 's/ W / T /' > symbols.txt
gprof -b -p --external-symbol-table=symbols.txt \
  "$profile_binary" gmon.out > profile.txt
```

Use the ordinary Release binary and the same simulator options for wall-time
measurements. Measure five runs after one warmup, keep profiler runs separate,
and compare schedule files byte-for-byte. The recorded profile also checks
that every job starts after submission, runs for its time limit, and that
simultaneous starts/completions never exceed the 128-node capacity.

## Standalone count-model comparison

The [first milestone results](count-model/README.md) record the correctness
checks and measured overhead of the original standalone count model. Current
code uses CapacityModel directly; the redundant CountModel wrapper was removed.

`compare_count_model.py` compares baseline and candidate binaries, with an
optional explicitly instrumented or historical binary supplied via `--hooks`.
It checks exact schedule/resource-history hashes on every run
and independently audits job counts, start/duration constraints, and capacity.
The inputs are generated with fixed seeds; no external trace is needed.

Prepare a source archive of the baseline revision and configure separate build
directories for the baseline and candidate. Use identical
compiler and optimization settings, including the same optional dependencies.
For the initial experiment, the shared build options are:

```bash
cmake -S SOURCE -B BUILD \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DDR_EVT_WITH_SER20=OFF -DDR_EVT_BUILD_PYTHON=OFF \
  -DDR_EVT_ENABLE_PROTOBUF=OFF -DDR_EVT_ENABLE_GRPC=OFF \
  -DDR_EVT_WITH_UNIT_TESTING=OFF -DDR_EVT_LEGACY_RESERVATION_BARRIER=OFF
cmake --build BUILD --target sim-bin -j4
```

By default, production resource operations use `CapacityModel<>`, with
`NoResourceObserver`. Enable per-simulation counters at runtime with
`--resource_model_diagnostics` (or the corresponding SimParams/protobuf field).
Construction selects `CapacityModel<SimulationResourceObserver>` for event
accounting and for the two CapacityModel-based scheduling traversals. Tests can
still select counting or custom recording observers explicitly. `if constexpr`
removes observation code for the default specialization.

Counters belong to the simulation's trace and record operation calls, requested
quantities, and failed fits without per-event heap allocation. They are available
in CLI statistics, the Python/C++ snapshot API, and gRPC statistics responses.
They do not automatically validate invariants. The native test checks runtime
selection, independent owners, delayed-release accounting, and checkpoint reset.
See [Resource diagnostics](../../../docs/dev/RESOURCE_DIAGNOSTICS.md) for scope
and dispatch costs.

The checked-in three-variant reports describe earlier source snapshots that
used `DR_EVT_COUNT_MODEL_HOOKS` to enable whole-simulation observation. That
CMake flag was renamed and then removed. Reproducing those exact binaries
requires their historical source snapshots.

Run the comparison after all builds/tests have finished, without concurrent
compilation or profiling:

```bash
python3 validation/performance/simulation/compare_count_model.py \
  --baseline /tmp/count-baseline-build/simulator \
  --model /tmp/count-model-build/simulator \
  --outdir /tmp/count-model-measurement \
  --revision BASELINE_REVISION \
  --compiler 'COMPILER_VERSION' \
  --build-settings 'Release -O3; static; LTO off; optional dependencies off' \
  --runs 9
```

For current builds, use the same candidate executable for `--model` and
`--hooks`, and add `--runtime-diagnostics`. The driver passes
`--resource_model_diagnostics` only to the hooks variant. Omit
`--runtime-diagnostics` for historical binaries with compile-time observation.
Timings include the enabled variant's counter-report output.

The four 2,000-job mixed cases exercise per-pass FCFS, EASY, HYBRID R32/Q64,
and conservative scheduling. A separate 8,000-job conservative case stresses
the per-pass traversal with unlimited Q. The existing 20,000-job fixed-dispatch
conservative profile remains a separate case; its scheduler calls are not yet
migrated to `CapacityModel`, while it exercises the extracted shared timeline and
committed resource accounting. `--per-pass-jobs` and `--stress-jobs` can change
these sizes independently.

`comparison.json` records binary/input/output hashes, commands, build metadata,
individual wall/CPU samples, medians, and paired percentage changes with
bootstrap intervals. The unobserved model versus baseline measures the
extraction. When `--hooks` is provided, its comparison with the candidate
measures observation overhead. Results for short runs can be dominated by
startup, output, or machine variation. A noisy
interval is not evidence that a small regression has been ruled out.

For short cases, use `--batch-size 20 --cases mixed-none mixed-easy mixed-hybrid
mixed-conservative`. Each timing sample averages 20 executions; every execution
still receives the output-hash checks. Run the two stress cases separately with
the default batch size: `--cases stress-per-pass-conservative
stress-cached-conservative`. This avoids repeating long runs unnecessarily.

Build/run `test_capacity_model-bin` to check the independent interval oracle,
explicit recording/counting observers, and delayed-release integration.
