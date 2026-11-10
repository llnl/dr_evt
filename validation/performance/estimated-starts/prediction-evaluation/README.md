# Reproduce the start-prediction evaluation

See [the comparison](COMPARISON.md), [full tables](cache-first/NORMALIZED_RESULTS.md), and [summary metrics](cache-first/summary.json). Raw measurements are generated locally.

The benchmark evaluates four methods across ten configurations and two duration conditions. Inputs come from `docs/_static/scheduler-validation/performance/cases.json`; `run.py` adds four configurations.

From the repository root, configure a Release build, then run the following. Replace `/path/to/boost/include` with your Boost header directory:

```bash
cmake --build build --target dr_evt
c++ -O3 -DNDEBUG -g -fno-omit-frame-pointer -std=c++20 \
  -Isrc -Ibuild -Ibuild/src -Iinstall/include -I/path/to/boost/include \
  validation/performance/estimated-starts/prediction-evaluation/evaluate_observable.cpp \
  -Lbuild -ldr_evt -Wl,-rpath,"$PWD/build" -o /tmp/evaluate_predictions
python3 validation/performance/estimated-starts/prediction-evaluation/run.py \
  /tmp/evaluate_predictions --repo "$PWD" --observable \
  --outdir /tmp/dr_evt_prediction_results \
  --workdir /tmp/dr_evt_prediction_runs
```

`--observable` records that side simulation uses only submitted jobs and time-limit estimates for unfinished durations. Actual durations establish observed history and the reference execution.

The output directory contains summaries, tables, and raw `evidence/`; the work directory contains traces and logs. Keep these generated directories outside the repository. Timings vary by machine and build; summaries record platform, revision, and hashes.

Add `--reanalyze` to recalculate metrics from a locally generated run without repeating the simulations.
