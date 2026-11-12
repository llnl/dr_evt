# Resource diagnostics

Enable `resource_model_diagnostics` before constructing a simulation; it defaults
to false. The setting is available through the CLI
(`--resource_model_diagnostics`), C++, Python, protobuf, and gRPC.

The trace owns per-simulation counters for operation calls, requested quantities,
and failed fits. Construction selects observed or unobserved specializations;
`if constexpr` removes unused observer code from resource operations. Trace event
entry points still use indirect dispatch. See [Resource models](RESOURCE_MODELS.md).

Per-pass FCFS and the OpenPBS one-second forecast report fit, reservation, and
projected depletion operations. All trace policies report allocation, completion,
cleanup holds, and delayed release. Other scheduler policies do not report
fit/reservation counts.

Read an owning snapshot with C++/Python `get_resource_diagnostics()`, or use gRPC
statistics. Snapshots are absent when disabled. CLI statistics print enabled
counters; aggregate statistics JSON does not include them. Counters reset after
a successful checkpoint load using the destination's construction setting.

Explicit template observers remain available for tests and custom diagnostics.
Their context pointers are borrowed only for the synchronous observation call;
`CountingResourceObserver` uses static test counters, separate from simulation
counters.
