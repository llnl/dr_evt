# FCFS comparison using sched-simple

The [Scheduling Validation page](../../docs/user-guide/SCHEDULER_VALIDATION.md#fcfs-without-backfill-identical-sched-simple-run)
contains the completed 2,000-job Lassen comparison, resource curves, metrics,
and downloadable plotting data. The [harness README](README.md) gives the
container setup and reproduction commands.

The validation-only [wrapper](sched_simple.py) inherits Flux Core's native
allocation, FIFO scheduling, and forecast methods unchanged. It adds the
read-only synchronization and resource snapshots needed by Fiction.
Thirty [small comparisons](test_sched_simple.py) pass at Q=1, Q=2, and Q=64.
