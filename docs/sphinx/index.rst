DR_EVT Scheduler Documentation
================================

.. toctree::
   :maxdepth: 2
   :caption: User Documentation

   user_guide
   quickstart
   trace_formats
   testing

.. toctree::
   :maxdepth: 2
   :caption: Developer Documentation

   api_overview
   scheduler_design
   test_descriptions
   contributing

.. toctree::
   :maxdepth: 2
   :caption: Test Results

   test_summary
   backfill_verification
   saturation_results

Overview
--------

DR_EVT (Discrete Resource Event Modeling) is a high-performance HPC job scheduler
simulator implementing SLURM-style backfilling algorithms.

Features
--------

* **Backfilling Algorithms**: EASY and Conservative backfill policies
* **Priority Policies**: FCFS, Shortest-Job-First, Longest-Job-First
* **Runtime Modes**: Oracle (USE_ACTUAL) and realistic (USE_LIMIT)
* **Trace Formats**: Simple 7-column format and LLNL Lassen 33-column format
* **Timestamp Support**: Unix epoch and ISO 8601 with timezone awareness
* **Comprehensive Testing**: 15 automated tests with 100% pass rate

Quick Start
-----------

.. code-block:: bash

   # Run basic simulation
   ./simulator trace.csv \\
     --total_nodes 100 \\
     --backfill_policy easy \\
     --priority_policy fcfs \\
     --runtime_mode actual

   # Run test suite
   ./tests/run_tests.sh
   python3 tests/test_scheduler.py

Installation
------------

.. code-block:: bash

   # Build simulator
   mkdir build && cd build
   cmake ..
   make -j4

   # Run tests
   cd ..
   ./tests/run_tests.sh

Documentation Sections
----------------------

**User Documentation**
   Complete guides for using the simulator, creating traces, and running simulations.

**Developer Documentation**
   Architecture, API documentation, and contribution guidelines.

**Test Results**
   Comprehensive test validation results and performance analysis.

Performance
-----------

* **Small traces** (< 100 jobs): < 10ms
* **Medium traces** (100-1000 jobs): 10-100ms
* **Stress test** (30 jobs, complex): ~2ms
* **Speed-up**: > 1,000,000× vs real-time

Indices and Tables
==================

* :ref:`genindex`
* :ref:`search`
