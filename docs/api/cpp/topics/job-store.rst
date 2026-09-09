Job Store API
=============

The job store is owned by ``Trace``. It holds ``Job_Record`` instances,
supports batch and streaming insertion, reclaims completed front records, and
writes simulated-job output.

.. only:: doxygen

   .. doxygenclass:: dr_evt::Trace
      :project: dr_evt
      :members:

   .. doxygenclass:: dr_evt::Job_Record
      :project: dr_evt
      :members:

See `Output-Trace Buffers and Streaming Trace State <../../../dev/OUTPUT_TRACE_BUFFERS.html>`_
for the storage and reclamation model.
