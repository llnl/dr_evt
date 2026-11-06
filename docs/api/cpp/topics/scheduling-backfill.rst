Scheduling and Backfill API
===========================

``SchedulerBase``, the FCFS/SJF/LJF schedulers, wait queues, and schedule
windows implement priority ordering with EASY, hybrid, or conservative
backfilling. Event-driven and unified periodic dispatch are separate trigger
modes; see `Scheduling Loops <../../../dev/SCHEDULER_LOOPS.html>`_.

.. only:: doxygen

   .. doxygengroup:: dr_evt_sim
      :project: dr_evt
      :content-only:
      :inner:
      :members:
      :protected-members:
      :private-members:

See `Wait Queues <../../../dev/WAIT_QUEUES.html>`_ and
`Backfilling Algorithms <../../../BACKFILLING_ALGORITHMS.html>`_ for the
implementation and algorithm guides.
