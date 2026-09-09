# Developer Documentation

This section contains implementation notes, architectural rationale, and
maintainer-facing build guidance. It is separate from the user documentation.

## What Goes Here

### [Design decisions](design-decisions/README.md)

Important decisions worth preserving in the repository:

- Why certain approaches were chosen
- Breaking changes and migration guides
- Terminology changes and rationale

### [Job lifecycle](JOB_LIFECYCLE.md)
How a new job moves through the Trace job store, scheduler wait queue, and
scheduled start/end event queue. This is the companion explanation for the
Doxygen comments on `append_job`, `submit_job`, and the two `insert_job`
methods.

```{toctree}
:maxdepth: 2

JOB_LIFECYCLE
BLOCK_WAIT_QUEUE
design-decisions/README
```
