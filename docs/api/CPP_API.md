# C++ API Reference

This reference is generated from the public C++ headers by Doxygen and
rendered by Sphinx, so it shares the site's theme, navigation, and search.

<div class="api-search" role="search">
  <label for="cpp-api-map-search">Search C++ API map</label>
  <input id="cpp-api-map-search" data-api-search="cpp-api-map-content" type="search" placeholder="e.g., Trace, backfill, gRPC" autocomplete="off">
  <span class="api-search-status" aria-live="polite"></span>
</div>

<div id="cpp-api-map-content" class="api-search-content api-search-map">

## Browse by responsibility

Use this task-oriented map to open the relevant part of the generated
reference:

- [Job store](cpp-api-reference.rst) — `Trace`, `Job_Record`,
  streaming job insertion, capacity, reclamation, and simulated-job output.
- [Trace parsing and replay](cpp-api-reference.rst) — input
  columns, timestamps, loading, `DR_Event`, and replay-oriented handling.
- [Resource trace](cpp-api-reference.rst) — resource-history
  samples and resource-trace output.
- [Scheduling and backfill](cpp-api-reference.rst) — schedulers,
  backfill policies, wait queues, and schedule windows.
- [Simulator](cpp-api-reference.rst) — `Simulation`, event
  advancement, statistics, and streaming submission.
- [Client, server, and orchestration](cpp-api-reference.rst) — gRPC
  services/clients and MPI orchestration.
- [Bindings](PYTHON_API.md) — the Python extension's supported `Simulation`
  and `Sim_Params` surface.
- [Input options and configuration](cpp-api-reference.rst) —
  `Sim_Params`, `Trace_Params`, command-line parsing, and protobuf adapters.
- [Utilities](cpp-api-reference.rst) — exceptions, file and
  memory helpers, random number generation, timers, and stream adapters.

## Browse by source directory

- [src](cpp-api-reference.rst) — shared types, common facilities, and the
  top-level simulator entry points.
- [src/sim](cpp-api-reference.rst) — simulation control, scheduler policies,
  backfill logic, wait queues, and schedule windows.
- [src/trace](cpp-api-reference.rst) — job storage, input parsing, replay,
  event records, statistics, and job/resource trace output.
- [src/params](cpp-api-reference.rst) — command-line and file-based simulation
  and trace configuration.
- [src/proto](cpp-api-reference.rst) — gRPC service/client and protobuf
  configuration adapters.
- [src/mpi](cpp-api-reference.rst) — MPI feeder and multi-server orchestration.
- [src/utils](cpp-api-reference.rst) — supporting exceptions, filesystem and
  memory helpers, random-number utilities, timers, and stream adapters.

</div>

:::{only} doxygen
<p><a href="cpp-api-reference.html">Open the complete generated C++ API reference.</a></p>
:::

:::{only} not doxygen
The optional C++ API reference is unavailable because Doxygen was not run for
this build.
:::
