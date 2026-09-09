:orphan:

C++ API reference
=================

Browse the API by source directory, or filter the entries below by class,
function, enum, or file name.

Browse by responsibility
-------------------------

The generated declarations are also grouped by source directory below. Use this
map to begin with the subsystem you are working on:

- **Job store** — `Trace`, `Job_Record`, and `Job_Append_Request` own loaded
  and streaming job records, capacity, reclamation, and simulated-job output.
  See :ref:`src/trace <cpp-api-trace>`.
- **Trace parsing and replay** — trace columns, timestamps, input loading,
  `DR_Event`, and replay-oriented record handling live in
  :ref:`src/trace <cpp-api-trace>`.
- **Resource trace** — `Trace` records resource-history samples and writes the
  resource trace; the relevant declarations are in
  :ref:`src/trace <cpp-api-trace>`.
- **Scheduling and backfill** — `SchedulerBase`, FCFS/SJF/LJF schedulers,
  circular and block wait queues, and schedule windows live in
  :ref:`src/sim <cpp-api-sim>`.
- **Simulator** — `Simulation` coordinates trace loading, scheduling, event
  advancement, statistics, and streaming submission. See
  :ref:`src/sim <cpp-api-sim>`.
- **Client, server, and orchestration** — gRPC service/client code and
  multi-platform orchestration are in :ref:`src/proto <cpp-api-proto>` and
  :ref:`src/mpi <cpp-api-mpi>`.
- **Bindings** — the Python extension exposes selected `Simulation` and
  `Sim_Params` APIs through
  `python/dr_evt_bindings.cpp <https://github.com/LLNL/dr_evt/blob/main/python/dr_evt_bindings.cpp>`_.
  See the :doc:`Python API reference <PYTHON_API>` for its supported surface.
- **Input options and configuration** — `Sim_Params`, `Trace_Params`, command
  line parsing, and protobuf configuration adapters are in
  :ref:`src/params <cpp-api-params>` and :ref:`src/proto <cpp-api-proto>`.
- **Utilities** — exceptions, file and memory helpers, random number
  generation, timers, and stream adapters are summarized in
  :ref:`src/utils <cpp-api-utils>`.

.. raw:: html

   <div class="api-search" role="search">
     <label for="cpp-api-search">Search C++ API</label>
     <input id="cpp-api-search" data-api-search="cpp-api-content" type="search" placeholder="e.g., Simulation, Trace, append_job" autocomplete="off">
     <span class="api-search-status" aria-live="polite"></span>
   </div>
   <div id="cpp-api-content" class="api-search-content">

.. _cpp-api-src:

src
---

.. doxygengroup:: dr_evt_global
   :project: dr_evt
   :content-only:
   :inner:

.. _cpp-api-sim:

src/sim
-------

.. doxygengroup:: dr_evt_sim
   :project: dr_evt
   :content-only:
   :inner:

.. _cpp-api-trace:

src/trace
---------

.. doxygengroup:: dr_evt_trace
   :project: dr_evt
   :content-only:
   :inner:

.. _cpp-api-params:

src/params
----------

.. doxygengroup:: dr_evt_params
   :project: dr_evt
   :content-only:
   :inner:

.. _cpp-api-proto:

src/proto
---------

.. doxygengroup:: dr_evt_proto
   :project: dr_evt
   :content-only:
   :inner:

.. _cpp-api-mpi:

src/mpi
-------

.. doxygenfile:: mpi_job_feeder.cpp
   :project: dr_evt
   :path: src/mpi

.. _cpp-api-utils:

src/utils
---------

Utility headers include exceptions, file and system-memory helpers, random
number generation, timers, and stream-buffer adapters. Their API remains
available in the source tree; it is intentionally not expanded here because
several template stream adapters expose identical unqualified member signatures
that Doxygen/Breathe cannot index together reliably. See
`src/utils <https://github.com/LLNL/dr_evt/tree/main/src/utils>`_ for the
complete utility headers.

.. raw:: html

   </div>
