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
base. No placement class is introduced yet.

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
