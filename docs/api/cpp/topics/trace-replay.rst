Trace Parsing and Replay API
============================

Trace parsing, timestamp conversion, input columns, `DR_Event <cpp-dr-event_>`_, and replay
handling are provided by the trace subsystem.

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

   .. doxygenclass:: dr_evt::Data_Columns
      :project: dr_evt
      :members:
      :protected-members:
      :private-members:

For the operational distinction between replay and simulation, see
`Simulation vs. Replay Modes <../../../dev/design-decisions/SIMULATION_VS_REPLAY_MODES.html>`_.

.. _cpp-dr-event: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/trace-replay.html#_CPPv4N6dr_evt8DR_EventE
.. _cpp-trace: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/trace-replay.html#_CPPv4I0EN6dr_evt10BasicTraceE

.. _cpp-basictrace: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/trace-replay.html#_CPPv4I0EN6dr_evt10BasicTraceE
