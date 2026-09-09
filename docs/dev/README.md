# Developer Documentation

This directory contains development notes, and design decisions that are useful
for understanding the implementation history but are not part of the main user documentation.

## What Goes Here

### `design-decisions/`
Important design decisions worth preserving in the repository:
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

design-decisions/README
JOB_LIFECYCLE
BLOCK_WAIT_QUEUE
```
