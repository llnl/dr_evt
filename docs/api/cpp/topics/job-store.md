# Job Store API

The job store is owned by `Trace`. It holds `Job_Record` instances,
supports batch and streaming insertion, reclaims completed front records,
and writes simulated-job output.

The generated declarations are in [src/trace](../source/trace.rst). See
[Trace as a Streaming-Ready State Container](../../../dev/design-decisions/OUT_TRACE_STREAMING.md)
for the storage and reclamation model.
