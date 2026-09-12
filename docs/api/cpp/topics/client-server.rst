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

Examples, use cases, and tests
------------------------------

* `Example C++ client <https://github.com/LLNL/dr_evt/blob/main/src/proto/dr_evt_client.cpp>`_
  demonstrates the request sequence against the generated service API.
* `Client/server setup <../../../user-guide/grpc-setup.html#connecting-the-example-client>`_
  shows how to run the example client and server.
* `Client/server use cases <../../../user-guide/client-server-use-cases.html>`_
  link the Python multi-server, MPI-launcher, and synchronized-system examples.
* `Distributed client/server tests <https://github.com/LLNL/dr_evt/blob/main/tests/README.md#distributed-clientserver-tests>`_
  list the exact gRPC and MPI commands, fixtures, and coverage.
* `gRPC streaming test <https://github.com/LLNL/dr_evt/blob/main/tests/test_grpc_streaming_api.cpp>`_
  and `MPI client/server test <https://github.com/LLNL/dr_evt/blob/main/tests/test_grpc_multi_client_server.cpp>`_
  provide complete C++ API call sequences.
