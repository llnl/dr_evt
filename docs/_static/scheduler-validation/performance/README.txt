Common Docker processing-time measurements

All six DR_EVT comparisons use the same Release binary in one Linux Docker
container on an Apple M3 Max. Each case has one untimed warmup and five measured
runs. Cases run sequentially. Timing covers process launch, input, simulation,
schedule/resource output, and exit. Builds, Python parsing, audits, and plotting
are excluded. Every output matches its published schedule; repeated schedule
and resource files are byte-identical.

Files:
- performance.json: individual runs, medians, ranges, and output hashes.
- cases.json: original inputs, policy options, and expected schedules.
- measure_dr_evt.py: measurement and independent capacity/duration audit script.
- build.json: base revision, changed source hashes, flags, and binary hash.
- before-optimization.json and before-optimization-build.json: prior measurements.
- hotpath-review.txt: existing constexpr coverage and experiment details.
- constexpr-experiment.json: alternating runs of a rejected rounding specialization.
- ordered-insert-experiment.json: alternating runs of a rejected map insertion hint.
- environment.json: Docker image/version and Apple M3 Max hardware.
- native-hybrid.json: fresh Fiction hybrid time and published-schedule checks.
- native-build.json: cached native revisions, extensions, and binary hashes.

Reproduce the DR_EVT measurements from the repository root with Docker running:

  docker build -t dr-evt-flux-validation validation/flux
  mkdir -p validation/flux/results/common-timing
  docker run --rm -e DR_EVT_VALIDATION_WORKING_TREE=1 \
    --mount "type=bind,src=$PWD,dst=/dr_evt_source,readonly" \
    --mount "type=bind,src=$PWD/validation/flux/results/common-timing,dst=/results" \
    --mount type=volume,src=dr-evt-common-timing-build,dst=/workspace \
    dr-evt-flux-validation bash -c '
      python3 /dr_evt_source/validation/flux/build_dr_evt.py &&
      python3 /dr_evt_source/docs/_static/scheduler-validation/performance/measure_dr_evt.py \
        "$(cat /workspace/dr_evt-simulator)" --runs 5 --outdir /results/dr-evt-common'

The measured build snapshots tracked working-tree files. build.json records
the base revision and hashes of the changed source/test files; the remaining
compiled sources match that revision. The environment variable above includes
working-tree changes; omit it to build only committed sources. Use a fresh
result directory for each measurement.

The native hybrid replay used the cached Flux validation build volume v4.
Its pinned revisions and actual native binary/extension hashes are recorded
in native-build.json. The harness README at validation/flux/README.md provides
native setup. native-hybrid.json records the exact replay command. Its full
logs and outputs remain in the ignored validation/flux/results/timing-20261108
folder. The fresh native schedule matches the published hybrid schedule.

Other native timings are retained from their recorded runs. They include
scheduler-specific validation instrumentation and are single measurements.
