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

With ``DR_EVT_THREAD_PRIVATE_RNG`` enabled, ``RNGen::sample()`` uses a separate
engine for each OpenMP thread when callers provide thread-local distribution
objects. The stored distribution used by ``operator()`` and ``pull()`` remains
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

``serialize_binary()`` and ``deserialize_binary()`` use Ser20 with
caller-owned memory, avoiding an intermediate ``stringstream`` and string
copy. A ``std::vector<char>`` destination grows as needed and reuses reserved
capacity. Fixed one-byte spans, including ``std::span<std::byte>``, perform no
destination allocation and report the exact written byte count; pass only
that prefix to deserialization. ``bits()`` remains the direct native binary
path for trivially copyable scalars and vectors.

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
