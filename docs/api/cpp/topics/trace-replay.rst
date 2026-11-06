Trace Parsing and Replay API
============================

Trace parsing, timestamp conversion, input columns, ``DR_Event``, and replay
handling are provided by the trace subsystem.

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

   .. doxygenclass:: dr_evt::Data_Columns
      :project: dr_evt
      :members:
      :protected-members:
      :private-members:

For the operational distinction between replay and simulation, see
`Simulation vs. Replay Modes <../../../dev/design-decisions/SIMULATION_VS_REPLAY_MODES.html>`_.
