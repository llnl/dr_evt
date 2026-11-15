# Scheduler validation

Container-based comparisons with external schedulers live here, separately
from DR_EVT's unit and regression tests. Each harness records its source
revisions, configuration, local extensions, and results. A passing case
validates only the behavior exercised by that case.

- [Flux-Fiction](flux/README.md)
- [Native Slurm with periodic capacity and exact-placement backfill](slurm/README.md)
- [Native OpenPBS scheduler library with capacity and exact-placement cases](openpbs/README.md)
