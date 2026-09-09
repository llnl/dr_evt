Client, Server, and Orchestration API
=====================================

The gRPC service and client implementations expose streaming simulation over
the network; the MPI feeder supports multi-server orchestration.

.. only:: doxygen

   .. doxygengroup:: dr_evt_proto
      :project: dr_evt
      :content-only:
      :inner:

   .. doxygenclass:: MpiFeederSimulation
      :project: dr_evt

See the `Client/Server Guide <../../../CLIENT_SERVER_GUIDE.html>`_ for
deployment and wire-protocol usage.
