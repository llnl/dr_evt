Resource Trace API
==================

``Trace`` records finalized resource-history samples and writes resource-trace
output. Resource history is a separate circular buffer from the job store.

.. only:: doxygen

   .. doxygentypedef:: dr_evt::Trace
      :project: dr_evt

   .. doxygenclass:: dr_evt::BasicTrace
      :project: dr_evt
      :members:

   .. doxygenclass:: dr_evt::DR_Event
      :project: dr_evt
      :members:

See `Output Trace Files <../../../user-guide/output-traces.html>`_.
