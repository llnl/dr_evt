# Checked validation evidence

Every DR_EVT run in these reports uses `--per_pass_accounting` and the default
resource-time reservation rule. The reports retain the tested binary hash,
native revisions, applied settings, and recorded results where available.

- `lassen-comparisons.json`: the identical 2,000-job FCFS run with sched-simple
  and the hybrid R=32/Q=64 run with Fluxion, including metrics and completion status.
- `sched-simple-small.json`: thirty matching small cases at Q=1, Q=2, and Q=64.
- `minimal-lonodex.json` and `minimal-hinodex.json`: eighteen checks per matcher,
  including three expected four-job divergences, three matching input-order
  controls, and twelve matching single-job deletion controls.
- `placement-oracle.json`: native Fluxion node assignments and reservation
  snapshots for 60 whole-node cases. This reference runs independently of
  DR_EVT; see [the oracle contract](../PLACEMENT_ORACLE.md).

The [validation page](../../../docs/user-guide/SCHEDULER_VALIDATION.md) presents
the results and node-assignment diagram. Its figure folders contain reproducible plotting
inputs and exact numbers. Raw source traces and unresolved retry investigations
are outside this evidence package.
