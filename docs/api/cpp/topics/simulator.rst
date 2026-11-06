Simulator API
=============

``Simulation`` coordinates trace initialization, job submission, event-time
advancement, scheduling, statistics, and output generation.

.. if-doxygen::

   ``Simulation`` is the standard-trace alias
   ``BasicSimulation<Trace>``.

   .. doxygenclass:: dr_evt::BasicSimulation
      :project: dr_evt
      :members:
      :protected-members:
      :private-members:

For incremental use, see the `Streaming API <../../STREAMING_API.html>`_.
