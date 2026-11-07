Utilities API
=============

This category includes random-number generation, seed construction,
available-memory queries, monotonic timing, and path helpers.

.. if-doxygen::

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
      :private-members:

   Binary state serialization
   --------------------------

   ``serialize_binary()`` and ``deserialize_binary()`` archive custom Ser20
   structures directly to caller-owned memory. Use ``std::vector<char>`` for
   growable storage, reserving expected space to avoid reallocations, or a
   one-byte ``std::span`` (including ``std::span<std::byte>``) for fixed
   storage. Serialization returns the exact byte count; pass only that written
   prefix when deserializing. An undersized fixed buffer raises
   ``std::length_error``.

   These adapters avoid the extra whole-archive copy made by a
   ``std::stringstream``/``std::string`` round trip. Ser20 may still stage
   individual archive writes internally. ``bits()`` remains available as the
   minimal raw-representation path for trivially copyable scalars and vectors.
   Raw representations are native-endian and ABI-dependent.

   Other utilities
   ---------------

   .. doxygenfile:: system_memory.hpp
      :project: dr_evt

   .. doxygenfile:: timer.hpp
      :project: dr_evt

   .. doxygenfile:: file.hpp
      :project: dr_evt
