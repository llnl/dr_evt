# Custom Backfill Hook

DR_EVT provides a custom scheduler extension so experiments can change
backfill selection while reusing the simulator's event handling, job lifecycle,
and resource accounting. Power-aware backfill is a motivation for this design:
an experiment can choose which feasible jobs start based on their expected
power demand, rather than queue order alone.

## Decision: separate feasibility from experimental selection

The callback-based `FCFSEasyCustomScheduler` keeps ordinary FCFS dispatch and
EASY head protection in the simulator. When the head cannot start, it finds
later jobs that fit the current resource pool without delaying the protected
reservation, then lets a caller choose one of those feasible candidates.

A job-cost callback computes a value when the job enters the scheduler. The
selector receives `(job_id, cost)` pairs and returns an offered job ID or no
selection. The scheduler validates that the returned ID is a candidate before
starting it. This keeps experimental ranking separate from the common
resource and reservation checks.

The scan and candidate limits control different work:

- `backfill_queue_depth` bounds waiting-job examinations before feasibility
  checks; zero is unlimited.
- `num_max_candidates` caps feasible jobs offered to the selector. It must be
  positive and does not limit the preceding queue scan.

The hook is installed through the C++ or Python simulation constructor;
setting `num_max_candidates` in a CLI or Protobuf configuration does not
activate it. The callback implementation supports EASY or no backfill with
event-driven scheduling. See the [Python API](../../api/PYTHON_API.md) for
callback signatures and [Scheduling Policies](../../BACKFILLING_ALGORITHMS.md)
for candidate-limit semantics.

## Power-management use

A power-aware experiment can associate each job ID with a power estimate and
use that estimate as its cost. Its selector can rank feasible backfill jobs
to shape aggregate power usage, or decline candidates that exceed the
experiment's remaining power budget.

For decisions that depend on changing power demand, use current experiment
state rather than treating the insertion-time cost as a live measurement.
The C++ subclass interface provides hooks for newly eligible jobs, candidate
preparation, selection, and completed scheduling cycles. Selection can inspect
the current simulation time and effective running-job set, including jobs
selected to start earlier in the same scheduling call.

The [power-usage trace model](EXPERIMENTAL_TRACE_TYPES.md) supplies per-job
power data and aggregate power output. Recording power by itself does not
install a power-management policy. A backfill selector also does not control
ordinary FCFS head starts, so a hard cluster-wide power cap requires admission
control for those starts as well.

## Trade-offs and boundaries

Callbacks and experimental state add work only when the custom scheduler is
selected. Candidate caps bound the number of choices exposed to the selector,
but a small cap can hide a better power-management choice deeper in the queue.
More general scheduler subclasses can be injected through the C++ simulation
constructor, at the cost of owning and validating their additional behavior.

This existing scheduler hook changes scheduling policy; it does not replace
the aggregate resource model. The [resource-model hook](RESOURCE_MODEL.md)
still needs to be designed and implemented.

The implementation is in `src/sim/scheduler_fcfs_easy_custom.{hpp,cpp}` and
`src/sim/sim.hpp`. Tests in `tests/test_custom_scheduler.cpp` check candidate
validation, FCFS-head behavior, resource accounting, and callback selection.
