# Individual monthly warm-start EASY+PC jobs

This suite evaluates the EASY+PC reference policies described by
Carastan-Santos et al., *Scheduling With Lightweight Predictions in
Power-Constrained HPC Platforms* (IEEE TPDS, 2025;
DOI `10.1109/TPDS.2025.3586723`). Both policies retain FCFS ordering and the
standard EASY head reservation while requiring every new FCFS-prefix and
backfill start to fit an aggregate power cap.

- `mean` admits jobs using the sum of their `avgpcon` values.
- `max` admits jobs using the sum of their `maxpcon` values.

The implementation scans the complete EASY-feasible waiting queue in FCFS
order. It does not use EASYPower's dynamic target, horizon prediction,
candidate ranking, candidate limit, or candidate time window. The Gaussian
variants from the paper are not included because the Fugaku traces do not
contain per-job power standard deviations or within-job power time series.

The default cap is 12 MW because that is the existing study parameter; it is
not asserted to be a documented physical Fugaku limit. Use the same
`MAXIMUM_POWER` for both policies and every comparison run.

Prepared monthly inputs, capacity schedules, and EASY baselines are reused
from `individual-monthly-warm-start-sweep`. Results default to:

```text
experimental/fugaku-power/results/individual-monthly-warm-start-easy-pc-sweep
```

Generate the jobs and submit unfinished runs with:

```bash
./generate_job_set.py
EASYPOWER_DRIVER=/path/to/separate-build/easypower_experiment ./run_n_left.sh 12
```

Each complete result is stored as `OUTPUT_ROOT/YYYY-MM/easy-pc-{mean|max}`
and contains nonempty `jobs.csv` and `resources.csv` files plus a `.complete`
marker.
