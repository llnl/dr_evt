Resource Trace API
==================

`Trace <cpp-trace_>`_ records finalized resource-history samples and writes resource-trace
output. Resource history is a separate circular buffer from the job store.

.. if-doxygen::

   `Trace <cpp-trace_>`_ is the standard-policy alias
   `BasicTrace<Standard_Trace_Policy> <cpp-basictrace_>`_.

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

.. _cpp-trace: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/trace-replay.html#_CPPv4I0EN6dr_evt10BasicTraceE

.. _cpp-basictrace: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/trace-replay.html#_CPPv4I0EN6dr_evt10BasicTraceE
