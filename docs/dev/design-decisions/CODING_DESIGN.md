# Scheduler Specialization and Coding Design

DR_EVT relies heavily on C++ templates to specialize scheduler and event-loop
behavior. The purpose is to keep repeated scheduling work efficient in both
computation and memory while supporting several behavior choices.

## Why specialize hot paths

A scheduler can examine the same waiting jobs over many passes. A general
implementation that repeatedly tests every enabled behavior would charge
that selection cost for each examination or event. A universal job record
could also carry fields needed by only a subset of simulations.

Those costs accumulate even in one cluster and multiply when several cluster
simulations run simultaneously. Together with the
[aggregate resource model](RESOURCE_MODEL.md), specialization keeps the
ordinary simulation paths compact.

## Current decision: select specialized implementations

Configuration selects a concrete scheduler or simulation implementation
outside the repeated per-job selection work. Templates and `if constexpr`
compile the relevant behavior into those implementations, allowing unused
branches to disappear from the hot path. Shared template algorithms preserve
common scheduling logic without requiring a general mode check for every job.

Examples in the current implementation include:

- The event loop specializes resource accounting, warm-start processing,
  periodic backfill, fixed dispatch, and arrival notifications. The ordinary
  execution stage avoids the warm-start check after historical jobs depart.
- The periodic hybrid scheduler uses `ProtectReservations` to specialize
  protected backfill and the Slurm license-off diagnostic. The diagnostic
  path omits future reservation work without adding a per-job diagnostic check
  to protected scheduling.
- Shared scheduling templates select the reservation timeline or forecast
  implementation, including the OpenPBS one-second future-event grouping.
- [Experimental trace types](EXPERIMENTAL_TRACE_TYPES.md) specialize the
  simulation and record layout. Standard jobs carry no power-usage fields or
  per-job allocation for those experimental values; power records store their
  values inline.

Numeric limits, current resource availability, queue contents, and other
changing state remain runtime data. Specialization removes repeated choices
about implementation behavior; it does not make every scheduling decision a
compile-time decision or remove all runtime policy checks.

## Trade-offs and implementation guidance

Additional specializations can increase compile time and binary size. They
also produce many source files, typically one for each scheduler subclass.
This makes the implementation harder to navigate and increases maintenance
work: a shared behavior change may require checking several specialized paths
and their tests.

Keep common algorithms shared and specialize behavior where repeated branching
or unnecessary per-job storage would affect simulation throughput. Adding a
new behavior should preserve the cost of existing paths that do not use it.

Experiments that need custom backfill selection, including power management,
can use the [custom backfill hook](CUSTOM_BACKFILL_HOOK.md) instead of adding
their selection logic to every built-in scheduler specialization.

Verify scheduling results as well as performance when changing these paths.
The [Testing Guide](../../TESTING_GUIDE.md) describes correctness checks, and
[Performance Analysis](../PERFORMANCE_ANALYSIS.md) records measurements and
optimization rationale.

The main implementations are in `src/sim/sim.cpp`,
`src/sim/scheduler_fcfs_periodic_hybrid_circular.{hpp,cpp}`, and
`src/sim/scheduler_fcfs_hybrid_circular.tpp`; trace policies are defined in
`src/trace/trace.hpp`.
