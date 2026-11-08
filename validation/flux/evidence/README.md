# Checked validation evidence

Every DR_EVT run in these reports uses `--per_pass_accounting` and the default
resource-time reservation rule. The reports retain the tested binary hash,
native revisions, applied settings, and recorded results where available.

- `lassen-comparisons.json`: the identical 2,000-job FCFS run with sched-simple
  and the hybrid R=32/Q=64 run with Fluxion, including metrics and completion status.
- `sched-simple-small.json`: thirty matching small cases at Q=1, Q=2, and Q=64.
- `synthetic-fcfs-q64.json`: two matching 2,000-job FCFS traces.
- `synthetic-hybrid-q64.json`: metrics and provenance for the two 2,000-job
  hybrid R=32/Q=64 traces with identified aggregate differences.
- `minimal-lonodex.json` and `minimal-hinodex.json`: eighteen checks per matcher,
  including three expected four-job divergences, three matching input-order
  controls, and twelve matching single-job deletion controls.

The [validation page](../../../docs/user-guide/SCHEDULER_VALIDATION.md) presents
the results and updated slide. Its figure folders contain reproducible plotting
inputs and exact numbers. Raw source traces and unresolved retry investigations
are outside this evidence package.
