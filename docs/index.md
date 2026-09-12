# DR_EVT: HPC Job Scheduler Simulator

[![Documentation Status](https://readthedocs.org/projects/dr-evt/badge/?version=latest)](https://dr-evt.readthedocs.io/en/latest/?badge=latest)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/LLNL/dr_evt/blob/main/LICENSE)

**Discrete Event-Driven Simulator for High-Performance Computing Job Schedulers**

DR_EVT simulates HPC job scheduling policies with EASY and CONSERVATIVE backfilling
implementations. Uniquely supports **online simulation via gRPC**, enabling coordinated
multi-cluster simulations in a distributed fashion and digital-twin scheduler interacting in real-time.

Scheduler behavior is verified against a from-scratch Python reference implementation
(consistency check between implementations, not independently derived ground truth).

```{toctree}
:maxdepth: 2
:caption: Getting Started

getting-started/quickstart
getting-started/installation
getting-started/tutorial
```

```{toctree}
:maxdepth: 2
:caption: User Guide

user-guide/overview
user-guide/command-line
user-guide/protobuf-config
user-guide/trace-formats
user-guide/output-traces
user-guide/grpc-setup
user-guide/client-server-use-cases
user-guide/fugaku-power-experiment
```

```{toctree}
:maxdepth: 2
:caption: Algorithm & Testing

BACKFILLING_ALGORITHMS
TESTING_GUIDE
```

```{toctree}
:maxdepth: 2
:caption: APIs

api/STREAMING_API
api/CPP_API
api/PYTHON_API
CLIENT_SERVER_GUIDE
```

```{toctree}
:maxdepth: 3
:caption: Development

dev/README
dev/WAIT_QUEUES
dev/JOB_LIFECYCLE
dev/design-decisions/README
dev/READTHEDOCS_SETUP
```

```{toctree}
:maxdepth: 1
:caption: Reference

reference/terminology
```
