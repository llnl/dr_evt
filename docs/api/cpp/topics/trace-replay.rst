Trace Parsing and Replay API
============================

Trace parsing, timestamp conversion, input columns, ``DR_Event``, and replay
handling are provided by the trace subsystem.

.. only:: doxygen

   .. doxygenclass:: dr_evt::Trace
      :project: dr_evt
      :members:

   .. doxygenclass:: dr_evt::DR_Event
      :project: dr_evt
      :members:

   .. doxygenclass:: dr_evt::Data_Columns
      :project: dr_evt
      :members:

For the operational distinction between replay and simulation, see
`Simulation vs. Replay Modes <../../../dev/design-decisions/SIMULATION_VS_REPLAY_MODES.html>`_.
