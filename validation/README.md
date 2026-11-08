# Scheduler validation

Container-based comparisons with external schedulers live here, separately
from DR_EVT's unit and regression tests. Each harness records its source
revisions, configuration, local extensions, and results. A passing case
validates only the behavior exercised by that case.

- [Fiction](flux/README.md)
- [Native Slurm with periodic backfill and pooled capacity](slurm/README.md)
- [Native OpenPBS scheduler library with server-wide capacity](openpbs/README.md)
