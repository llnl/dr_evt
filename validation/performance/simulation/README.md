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
