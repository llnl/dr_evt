src/sim
-------

Filter the generated declarations in this source directory by class,
function, enum, or file name.

.. raw:: html

   <div class="api-search" role="search">
     <label for="cpp-sim-search">Search src/sim</label>
     <input id="cpp-sim-search" data-api-search="cpp-sim-content" type="search" placeholder="e.g., Simulation, Trace, append_job" autocomplete="off">
     <span class="api-search-status" aria-live="polite"></span>
   </div>
   <div id="cpp-sim-content" class="api-search-content">

.. doxygengroup:: dr_evt_sim
   :project: dr_evt
   :content-only:
   :inner:
   :members:
   :protected-members:
   :private-members:

.. doxygenfile:: scheduler_fcfs_periodic_hybrid_circular.hpp
   :project: dr_evt

Resource models
~~~~~~~~~~~~~~~

.. doxygenfile:: capacity_model.hpp
   :project: dr_evt

.. doxygenfile:: count_timeline.hpp
   :project: dr_evt

.. doxygenfile:: resource_model.hpp
   :project: dr_evt

Experimental multi-cluster scheduling
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. doxygenfile:: multi_system_backfill_scheduler.hpp
   :project: dr_evt

.. raw:: html

   </div>
