Resource Trace API
==================

``Trace`` records finalized resource-history samples and writes resource-trace
output. Resource history is a separate circular buffer from the job store.

.. if-doxygen::

   ``Trace`` is the standard-policy alias
   ``BasicTrace<Standard_Trace_Policy>``.

   .. doxygenclass:: dr_evt::BasicTrace
      :project: dr_evt
      :members:
      :protected-members:
      :private-members:

   .. doxygenclass:: dr_evt::DR_Event
      :project: dr_evt
      :members:
      :protected-members:
      :private-members:

See `Output Trace Files <../../../user-guide/output-traces.html>`_.
