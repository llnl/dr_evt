Job Store API
=============

The job store is owned by `Trace <cpp-trace_>`_. It holds `Job_Record <cpp-job-record_>`_ instances,
supports batch and streaming insertion, reclaims completed front records, and
writes simulated-job output.

.. if-doxygen::

   `Trace <cpp-trace_>`_ is the standard-policy alias
   `BasicTrace<Standard_Trace_Policy> <cpp-basictrace_>`_.

   .. doxygenclass:: dr_evt::BasicTrace
      :project: dr_evt
      :members:
      :protected-members:
      :private-members:

   .. doxygenclass:: dr_evt::Job_Record
      :project: dr_evt
      :members:
      :protected-members:
      :private-members:

See `Output-Trace Buffers and Streaming Trace State <../../../dev/OUTPUT_TRACE_BUFFERS.html>`_
for the storage and reclamation model.

.. _cpp-job-record: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/job-store.html#_CPPv4N6dr_evt10Job_RecordE
.. _cpp-trace: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/trace-replay.html#_CPPv4I0EN6dr_evt10BasicTraceE

.. _cpp-basictrace: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/trace-replay.html#_CPPv4I0EN6dr_evt10BasicTraceE
