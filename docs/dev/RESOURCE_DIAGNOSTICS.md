# Resource diagnostics

`resource_model_diagnostics` is a runtime construction setting, defaulting to
false. Enable it through `Sim_Params::m_resource_model_diagnostics`, Python's
`SimParams.resource_model_diagnostics`, the matching protobuf configuration or
gRPC InitRequest field, or the CLI's `--resource_model_diagnostics` flag.
It replaces the removed observer-selection CMake flag.

## Ownership and selection

The simulation's trace owns an optional `ResourceDiagnostics` allocation.
Enabled schedulers borrow this owner, which outlives them because the simulation
destroys its scheduler before its trace. No job-ID side map or per-job diagnostic
field is needed. Counters accumulate operation calls, requested quantities, and
failed fit attempts; they retain neither event records nor resource state.

Construction selects two specialized trace event-processing functions and, when
applicable, an observed placement-pass scheduler. These functions call either
`CountModel<>` or `CountModel<SimulationResourceObserver>`. `if constexpr` removes
observation code from the unobserved specialization. Individual resource
operations have no runtime enable check or observer dispatch.

Trace event-processing entry points use a member-function pointer chosen at
construction. This adds an indirect call at those entry points, including the
single-event entry point, and two dispatch pointers plus an optional counter
pointer per trace. Existing scheduler virtual dispatch is retained. Counters
allocate once when enabled; no diagnostic allocation occurs per operation.
This design does not establish zero overhead for event-entry dispatch; the
earlier CountModel benchmarks predate this runtime dispatch change.

## Coverage and access

Per-pass FCFS and the OpenPBS one-second forecast observe immediate/future fit,
reservation, and projected depletion. Other scheduler policies retain their
existing resource-search implementations, so they report no fit/reservation
counts. All trace policies observe committed allocation, completion, cleanup
holds, and delayed release. Warm-start processing can contribute counters;
forecast queries outside the selected live scheduler do not necessarily do so.

CLI statistics print counters when enabled. C++/Python
`get_resource_diagnostics()` returns an owning snapshot, absent when disabled.
gRPC statistics responses contain a corresponding optional message. Reading a
snapshot does not advance the simulation or perform a forecast. The aggregate
statistics JSON file does not include resource operation counters.

Counters belong to a single simulation. Independent sessions do not share them.
They are not continuation state: checkpoints record the setting, but counters
restart at zero after a successful load using the destination's construction
setting. Checkpoint format 21 includes the new configuration field.

## Explicit test observers

Tests can instantiate `CountModel<CountingResourceObserver>` or
`CountModel<RecordingObserver>` directly. The former's static test counters are
separate from simulation-owned runtime counters. Custom observers inspect
operation context synchronously; pointers in that context are valid only during
the observation call and must not become retained ownership.

The native count-model checks cover the interval oracle, explicit observers,
runtime enabled/disabled integration, delayed release, independent simulation
owners, and diagnostic reset after checkpoint restore. Python and gRPC checks
exercise the configuration and snapshot surfaces.
