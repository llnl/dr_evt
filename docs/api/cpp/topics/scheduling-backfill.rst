Scheduling and Backfill API
===========================

`SchedulerBase <cpp-schedulerbase_>`_, the FCFS/SJF/LJF schedulers, wait queues, and schedule
windows implement priority ordering with EASY, hybrid, or conservative
backfilling. Event-driven and fixed periodic dispatch are separate trigger
modes; see `Scheduling Loops <../../../user-guide/SCHEDULER_LOOPS.html>`_.

.. if-doxygen::

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

.. _cpp-schedulerbase: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/scheduling-backfill.html#_CPPv4N6dr_evt13SchedulerBaseE
