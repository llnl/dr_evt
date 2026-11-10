Utilities API
=============

This category includes random-number generation, seed construction,
available-memory queries, monotonic timing, and path helpers.

.. if-doxygen::

   Random-number generation
   ------------------------

   `RNGen <cpp-rngen_>`_ owns the engine stream used by simulation run-time sampling and
   supports serialization of that stream. Distribution formulas are documented
   by `BasicSimulation::sample_run_time() <cpp-basicsimulation-sample-run-time_>`_.

   When ``DR_EVT_THREAD_PRIVATE_RNG`` is enabled, `RNGen::sample() <cpp-rngen-sample_>`_ selects a
   separate engine for each OpenMP thread. Concurrent callers must also use
   separate distribution objects. `RNGen::operator() <cpp-rngen-operator_>`_ and `RNGen::pull() <cpp-rngen-pull_>`_
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

   `serialize_binary() <cpp-serialize-binary_>`_ and `deserialize_binary() <cpp-deserialize-binary_>`_ archive custom Ser20
   structures directly to caller-owned memory. Use ``std::vector<char>`` for
   growable storage, reserving expected space to avoid reallocations, or a
   one-byte ``std::span`` (including ``std::span<std::byte>``) for fixed
   storage. Serialization returns the exact byte count; pass only that written
   prefix when deserializing. An undersized fixed buffer raises
   ``std::length_error``.

   These adapters avoid the extra whole-archive copy made by a
   ``std::stringstream``/``std::string`` round trip. Ser20 may still stage
   individual archive writes internally. `bits() <cpp-bits_>`_ remains available as the
   minimal raw-representation path for trivially copyable scalars and vectors.
   Raw representations are native-endian and ABI-dependent.

   .. doxygenfunction:: dr_evt::bits
      :project: dr_evt

   .. doxygenfunction:: dr_evt::serialize_binary(const T &, std::vector<char> &)
      :project: dr_evt

   .. doxygenfunction:: dr_evt::serialize_binary(const T &, std::span<CharT>)
      :project: dr_evt

   .. doxygenfunction:: dr_evt::deserialize_binary(T &, std::span<const CharT>)
      :project: dr_evt

   .. doxygenfunction:: dr_evt::deserialize_binary(T &, const std::vector<char> &)
      :project: dr_evt

   Other utilities
   ---------------

   .. doxygenfile:: system_memory.hpp
      :project: dr_evt

   .. doxygenfile:: timer.hpp
      :project: dr_evt

   .. doxygenfile:: file.hpp
      :project: dr_evt

.. _cpp-basicsimulation-sample-run-time: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation15sample_run_timeE7tdiff_t16DistributionTypedd
.. _cpp-bits: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4I_23raw_binary_serializableE4bits6bits_tIR1TER1T
.. _cpp-deserialize-binary: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4I0_16binary_characterE18deserialize_binaryvR1TNSt4spanIK5CharTEE
.. _cpp-rngen: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4II0E00EN6dr_evt5RNGenE
.. _cpp-rngen-operator: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4N6dr_evt5RNGenclEv
.. _cpp-rngen-pull: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4N6dr_evt5RNGen4pullEv
.. _cpp-rngen-sample: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4I0EN6dr_evt5RNGen6sampleEN12Distribution11result_typeER12Distribution
.. _cpp-serialize-binary: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4I0_16binary_characterE16serialize_binary6size_tRK1TNSt4spanI5CharTEE
