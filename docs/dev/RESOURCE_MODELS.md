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

A derived, contiguous 64-row grid provides the coarse early filter. Each row
unions the nodes free at any point in its bucket. Intersecting touched rows
produces an optimistic candidate set: failure proves infeasibility, while
success requires the exact interval search. This avoids false negatives even
when nodes become free at different moments within a bucket. Exact search
removes every node blocked anywhere in the requested interval and returns one
fixed allocation. Earliest-fit candidates are the requested time and exact
interval ends; accepted starts are never rounded to bucket boundaries.

The `CoarseFilter` template argument is a captureless constexpr lambda returning
`bool`. The default `EnableCoarseFilter` selects the grid; `DisableCoarseFilter`
or a custom `[] { return false; }` selects exact-only searches:

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
`PlacementTimeline` remains an alias for the default enabled timeline. No
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
representative workloads. This implementation provides CPU word parallelism;
it does not implement GPU execution.

## Placement validation and integration scope

`test_placement_model` checks bitmap word boundaries and selection, invalid
updates, observer metadata, borrowed-owner lifetime, exact release boundaries,
fractional times, and randomized placement for both filter specializations against an independent
dense node-by-time oracle. Every valid exact fit must pass the coarse filter.

The validation-only `test_flux_placement_driver` uses this model for FCFS,
EASY, HYBRID, and conservative queue traversal. Each job record owns its live
allocation; the planner borrows it until the pass ends, and completion releases
and clears that same allocation. The comparison runs both filter specializations and checks exact IDs, execution
times, and future reservations against all 60 captured native Fluxion cases.
CapacityModel is not an equality reference. See the
[Fluxion placement oracle](https://github.com/LLNL/dr_evt/blob/main/validation/flux/PLACEMENT_ORACLE.md).

The reusable model is not yet selectable by the simulator. CLI, protobuf,
Python, trace serialization, and checkpoints keep their capacity interfaces.
Connecting placement to those layers requires carrying allocation ownership
through existing START/END/RELEASE events, including delayed release after job
record reclamation. Cores, GPUs, topology, sharing, and multiple resource types
are future work.
