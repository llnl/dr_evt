Job Store API
=============

The job store is owned by ``Trace``. It holds ``Job_Record`` instances,
supports batch and streaming insertion, reclaims completed front records, and
writes simulated-job output.

.. if-doxygen::

   ``Trace`` is the standard-policy alias
   ``BasicTrace<Standard_Trace_Policy>``.

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
