src/utils
---------

This page exposes documented utilities, including the serializable random
engine and seed helpers used by simulation run-time sampling.

.. raw:: html

   <div class="api-search" role="search">
     <label for="cpp-utils-search">Search documented utilities</label>
     <input id="cpp-utils-search" data-api-search="cpp-utils-content" type="search" placeholder="e.g., memory, time, path" autocomplete="off">
     <span class="api-search-status" aria-live="polite"></span>
   </div>
   <div id="cpp-utils-content" class="api-search-content">

Random-number generation
~~~~~~~~~~~~~~~~~~~~~~~~

With ``DR_EVT_THREAD_PRIVATE_RNG`` enabled, `RNGen::sample() <cpp-rngen-sample_>`_ uses a separate
engine for each OpenMP thread when callers provide thread-local distribution
objects. The stored distribution used by `operator() <cpp-rngen-operator_>`_ and `pull() <cpp-rngen-pull_>`_ remains
shared. Without that build option, all draws require external synchronization.
Do not seed, reconfigure, serialize, or directly access engines concurrently
with sampling.

.. doxygengroup:: dr_evt_rng
   :project: dr_evt
   :members:
   :protected-members:
   :private-members:

Binary state serialization
~~~~~~~~~~~~~~~~~~~~~~~~~~

`serialize_binary() <cpp-serialize-binary_>`_ and `deserialize_binary() <cpp-deserialize-binary_>`_ use Ser20 with
caller-owned memory, avoiding an intermediate ``stringstream`` and string
copy. A ``std::vector<char>`` destination grows as needed and reuses reserved
capacity. Fixed one-byte spans, including ``std::span<std::byte>``, perform no
destination allocation and report the exact written byte count; pass only
that prefix to deserialization. `bits() <cpp-bits_>`_ remains the direct native binary
path for trivially copyable scalars and vectors.

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
~~~~~~~~~~~~~~~

.. doxygenfile:: system_memory.hpp
   :project: dr_evt

.. doxygenfile:: timer.hpp
   :project: dr_evt

.. doxygenfile:: file.hpp
   :project: dr_evt

.. raw:: html

   </div>

.. _cpp-bits: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4I_23raw_binary_serializableE4bits6bits_tIR1TER1T
.. _cpp-deserialize-binary: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4I0_16binary_characterE18deserialize_binaryvR1TNSt4spanIK5CharTEE
.. _cpp-serialize-binary: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4I0_16binary_characterE16serialize_binary6size_tRK1TNSt4spanI5CharTEE

.. _cpp-rngen-sample: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4I0EN6dr_evt5RNGen6sampleEN12Distribution11result_typeER12Distribution

.. _cpp-rngen-operator: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4N6dr_evt5RNGenclEv

.. _cpp-rngen-pull: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/utilities.html#_CPPv4N6dr_evt5RNGen4pullEv
