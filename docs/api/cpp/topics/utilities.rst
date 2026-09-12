Utilities API
=============

This category includes random-number generation, seed construction,
available-memory queries, monotonic timing, and path helpers.

.. only:: doxygen

   Random-number generation
   ------------------------

   ``RNGen`` owns the engine stream used by simulation run-time sampling and
   supports serialization of that stream. Distribution formulas are documented
   by ``BasicSimulation::sample_run_time()``.

   When ``DR_EVT_THREAD_PRIVATE_RNG`` is enabled, ``RNGen::sample()`` selects a
   separate engine for each OpenMP thread. Concurrent callers must also use
   separate distribution objects. ``RNGen::operator()`` and ``RNGen::pull()``
   share the stored distribution and are not safe for concurrent calls. Without
   thread-private RNG support, callers must synchronize all concurrent draws.
   Seeding, parameter changes, serialization, and direct engine access must not
   overlap sampling.

   .. doxygengroup:: dr_evt_rng
      :project: dr_evt
      :members:
      :protected-members:

   Other utilities
   ---------------

   .. doxygenfunction:: dr_evt::get_available_memory_bytes
      :project: dr_evt

   .. doxygenfunction:: dr_evt::get_time
      :project: dr_evt

   .. doxygenfunction:: dr_evt::extract_file_component
      :project: dr_evt

   .. doxygenfunction:: dr_evt::append_to_stem
      :project: dr_evt
