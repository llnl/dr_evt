# Resource models

`CapacityModel<Observer>` is a concrete, default-constructible, nonvirtual base
class for one aggregate capacity quantity. Scheduler and trace callers use it
directly; the former CountModel wrapper has been removed because it provided
the same types and operations without specialized behavior.

```cpp
dr_evt::CapacityModel<> model;
dr_evt::CapacityModel<>::State allocated = 0;
model.allocate(allocated, 2);
model.release(allocated, 2);
```

Construction creates an empty operation object. It does not create inventory,
own allocation state, or allocate an observer. Operations accept caller-owned
state and can be called through an instance or statically. Admission and valid
accounting updates remain the caller's responsibility, as documented by the
operation preconditions. Derived models are selected at compile time; this
base is not a runtime polymorphic ownership interface.

## Representation and ownership

The initial capacity model retains the existing scalar node-count type for
Request, State, and Allocation, and a scalar start time for Placement. It uses
the existing CountTimeline algorithm for future capacity. The model has no
resource identities, per-job optional fields, side maps, or additional storage.

| Data | Existing owner |
| --- | --- |
| Pending demand and duration | Scheduler queue and job records |
| Speculative capacity and reservations | Pass-local timeline |
| Committed allocated and cleanup quantities | Trace accounting context |
| Quantity retained until delayed release | Release event |
| Optional diagnostic counters | Simulation's trace |

Planning depletion and live accounting are separate states. Reserving future
capacity does not launch a job; committed START/END/RELEASE events update live
accounting. Moving the operations into CapacityModel adds no new synchronization
or cleanup path.

The model is an empty, nonpolymorphic type. Observation uses
`if constexpr`: `CapacityModel<>` omits observer work, while
explicit observer types or runtime simulation diagnostics retain their existing
behavior. See [Resource diagnostics](RESOURCE_DIAGNOSTICS.md).

`common.hpp` provides `DR_EVT_RESTRICT` for pointer declarations across GCC,
Clang (including Apple Clang), and Intel, with an empty fallback elsewhere.
Use it only after proving the compiler's no-alias contract; the macro does not
check overlapping accesses. Hourly submission statistics use it for separate
history and summary arrays during accumulation. Current capacity operations do
not need it.

## Scope and extension

This first base represents a single aggregate pool. Multiple quantities such as
memory, bandwidth, storage, cores, and GPUs need jointly feasible requests and
timelines; they are not implemented by this extraction. Placement-aware models
will additionally identify resources and enforce locality or topology rules.
They may use capacity accounting as a component rather than inherit this scalar
base. The whole-node bitmap model below is the first placement implementation.

CLI, protobuf, Python bindings, trace formats, and checkpoint layout retain their
current node-count interfaces. This change introduces an internal C++ base;
there is no new resource-selection option in those layers.

## Validation

The native capacity-model test instantiates the model directly,
runs its fitting/reservation operations against the independent
dense interval oracle, and checks observer snapshots and accounting boundaries.
Compile-time checks verify direct construction, scalar types, and absence of
virtual dispatch or fields. Runtime diagnostic, scheduling, delayed-release,
and checkpoint tests exercise CapacityModel through the scheduler and trace.

## Whole-node placement

`PlacementModel<Observer, NodeOrder, CoarseFilter>` is a concrete, stateless model. Requests
remain node quantities; allocations contain exact node identities. Its
`Bitmap` uses contiguous 64-bit words with zero padding bits. Selection takes
whole words at once and scans bits only in the final partial word. The default
order is `NodeOrder::LowIdFirst`; `HighIdFirst` is another compile-time
specialization. This order is independent of storage layout. Word-wise OR,
AND-NOT, AND, and XOR support bulk set operations, while `std::popcount` and bit
scans allow compiler-selected hardware instructions. Committed release clears
exact bits and rejects missing charges; it does not toggle them.

```cpp
using Model = dr_evt::PlacementModel<>;
Model::Timeline plan(dr_evt::Bitmap(128, true), 0);
auto placement = Model::find_fit(plan, 4, 60, 0);
if (placement) {
    Model::reserve(plan, std::move(*placement), 4, 60);
}
```

`PlacementTimeline` keeps exact half-open intervals with dense resource masks.
A future reservation owns its mask. `borrow_running()` references the caller's
existing allocation owner without copying its bitmap or changing running-job
metadata. Borrowed allocations must stay immutable and alive until the planning
pass and its copies finish. A committed allocation must remain with its owner
through completion and any delayed release. No job-ID side map is introduced.

`seed_running(records, describe)` admits a whole existing running-record range
into an empty timeline. The descriptor returns an interval borrowing each live
allocation. A temporary union checks disjointness, and the final interval
metadata sorts once. Invalid input or a failed descriptor leaves the timeline
unchanged; no additional running collection survives initialization.

Before any future reservation or outage exists, `find_available()` selects
directly from eligible nodes excluding live occupied nodes. The validation
driver constructs its timeline lazily when an immediate request fails and can
receive a reservation. Once constructed, the timeline governs all later queries
in that pass. This fast path requires the caller to enforce that precondition.
Earliest-fit queries reuse one local bitmap across candidate starts and transfer
it into the successful result. Boolean feasibility queries refine candidates
without constructing an allocation result.

The [placement optimization trials](https://github.com/LLNL/dr_evt/blob/main/validation/performance/simulation/placement-model/README.md)
measure these changes independently, including the low-load 20K-job trace and
focused reservation queries.

A derived, contiguous 64-row grid provides the coarse early filter. Each row
unions the nodes free at any point in its bucket. Intersecting touched rows
produces an optimistic candidate set: failure proves infeasibility, while
success requires the exact interval search. This avoids false negatives even
when nodes become free at different moments within a bucket. Exact search
removes every node blocked anywhere in the requested interval and returns one
fixed allocation. Earliest-fit candidates are the requested time and exact
interval ends; accepted starts are never rounded to bucket boundaries.

The `CoarseFilter` template argument is a captureless constexpr lambda returning
`bool`. The default `DisableCoarseFilter` selects exact-only searches and omits
the grid. Opt in with `EnableCoarseFilter` or a custom `[] { return true; }`.
An explicit exact-only selection also works:

```cpp
using ExactModel = dr_evt::PlacementModel<
    dr_evt::NoResourceObserver, dr_evt::NodeOrder::LowIdFirst,
    [] { return false; }>;
ExactModel::Timeline plan(dr_evt::Bitmap(128, true), 0);
auto placement = ExactModel::find_fit(plan, 4, 60, 0);
```

`BasicPlacementTimeline<CoarseFilter>` evaluates the selector at compile time.
The disabled storage specialization is empty; `[[no_unique_address]]` removes
its storage cost, and `if constexpr` omits grid filtering, invalidation, and
rebuilding. Both modes share the exact search and ownership rules.
`PlacementTimeline` remains an alias for the default exact-only timeline. No
runtime switch or preprocessor flag is stored in the resource model.

The enabled grid belongs to the pass-local timeline, is invalidated on insertion,
and is rebuilt lazily from temporary interval references. Copying a timeline copies
exact state and preserves borrowed owners, but drops the derived grid. Empty
allocations add no intervals. Nonpositive durations query an instant; reserving
such a duration is a no-op. Queries on a shared timeline are not thread-safe
because they may rebuild its cache.

Both models use `if constexpr` to omit disabled observer work. Placement order
also specializes at compile time. No speedup is claimed for the coarse filter:
its rebuild and storage costs must be measured against exact-only search for
representative workloads. Exact-only search is the default because rebuilding
the coarse cache can cost more than the exact searches it avoids. This implementation provides CPU word parallelism;
it does not implement GPU execution.

## Placement validation and integration scope

`test_placement_model` checks bitmap word boundaries and selection, invalid
updates, observer metadata, borrowed-owner lifetime, exact release boundaries,
fractional times, and randomized placement for both filter specializations against an independent
dense node-by-time oracle. Every valid exact fit must pass the coarse filter.

`test_flux_placement_driver` now drives the actual `Simulation` scheduler and
START/END/RELEASE event loop. All six optimization modes, both filter
specializations, low/high matching, FCFS, EASY, HYBRID and conservative policies
are compared with the 60 captured Fluxion cases. Checks include exact node IDs,
execution times and future reservations. CapacityModel is not an equality
reference. See the
[Fluxion placement oracle](https://github.com/LLNL/dr_evt/blob/main/validation/flux/PLACEMENT_ORACLE.md).

Select `--resource_model placement` in the simulator, `resource_model` in
protobuf configuration/InitRequest, or `SimParams.resource_model` in Python.
Capacity remains the default. Placement supports whole-node exclusive resources
with event-driven circular FCFS queues, static inventory and exact planning
times. Periodic/custom schedulers, replay warm start, capacity schedules,
per-pass accounting and rounded planning times are rejected explicitly.

The running-job store selects either its original count map or a placement map
at construction. Capacity records retain their original scalar layout. Each
placement record owns its bitmap until RELEASE, including cleanup after END and
trace-record reclamation. An occupied union is updated at commit/release for
immediate selection. Pending decisions transfer their masks into this owner;
planning borrows masks only until the pass ends. The retained reporting snapshot
owns future reservations. There is no second job-ID allocation map.

Checkpoints preserve exact running/cleanup allocations and the timestamp's
examination budget, validate allocation width/domain/cardinality/overlap and
restore the selected specialization. Like existing estimated-start caches,
reservation reporting snapshots are rebuilt after restore by scheduling.
JobStatus.node_ids reports live or reserved IDs in C++, Python and gRPC; released
IDs are not retained. CSV resource history remains aggregate accounting. A
sparse allocated/released-ID trace, cores, GPUs, topology, sharing and multiple
resource types are future work.

`test_placement_integration` exercises the main simulator's bitmap boundaries,
node order, delayed release, reclamation, diagnostics, bounded queues and
checkpoint continuation. Whole-simulator performance measurements remain pending; the historical
standalone placement trials do not measure this integration.

Aggregate queue-drain, horizon and uncached status forecasts remain available
as approximations in placement mode. They do not prove an exact placement fit.
