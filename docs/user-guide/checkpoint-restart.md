# Checkpoint/Restart

A checkpoint saves the simulator's logical state so a later run can continue
from the same scheduling boundary. Output is stored separately and reconstructed
from archived segments after restart. For initialization from a historical
schedule, see
[Maintenance, Capacity Changes, and Warm Starts](maintenance-and-warm-start.md).

## Requirements and compatibility

Checkpoint/restart is compiled when `DR_EVT_WITH_SER20=ON`. Checkpoints are
Ser20 binary, same-build artifacts rather than a portable exchange format. A
checkpoint records the complete [`Sim_Params`][cpp-sim-params] of the run that wrote it; the
destination must use matching scheduling, runtime, capacity, and output
configuration, which is validated field by field on restore, and the recorded
configuration is available afterwards through [`Simulation::restored_params()`][cpp-simulation-restored-params].
Callback-based Custom FCFS is supported when the destination
simulation is constructed with equivalent cost and selection callbacks. The
checkpoint restores its queue entries, previously computed costs, candidate
state, and accounting without invoking the cost callback during load; callback
objects and their externally owned state are not serialized. Custom scheduler
subclasses remain unsupported because they may add unknown state. Standard,
Pcon, and SWF traces and progressive file loading are supported; replay/warm-start
execution remains unsupported. Progressive file loading resumes at the next
fully admitted input file.

## Save and restore simulation state

Save all logical state after [`advance_to()`][cpp-simulation-advance-to] returns, outside scheduler callbacks,
and restore it into a newly constructed simulation with matching [`Sim_Params`][cpp-sim-params]:

```cpp
sim.advance_to(500.0);
sim.save_checkpoint("simulation.ckpt");

Simulation resumed(params);
resumed.load_checkpoint("simulation.ckpt");
resumed.advance_to(1000.0);
```

Checkpoints record the scheduling controls, including
`per_pass_accounting`, `diagnostic_no_reservations`, `dispatch_on_events`,
and the next tick deadline.
This build uses checkpoint format 20; older checkpoints must be regenerated.
Restart preserves the selected reservation and accounting policies, arrival
notification behavior, and timer phase.

The checkpoint includes resident job records, appended-job status history,
scheduler queue state, running jobs, pending events, capacity and statistics
accounting, resource history, and the random-number generator.

The [Python API](../api/PYTHON_API.md) exposes the same
[`save_checkpoint(filename)`][cpp-simulation-save-checkpoint] and [`load_checkpoint(filename)`][cpp-simulation-load-checkpoint] methods.

## Automatic checkpoints

Set `--checkpoint_file FILENAME` to select the automatic checkpoint destination.
Set `--checkpoint_interval_jobs COUNT` to save after each count of completed
jobs. Progressive loading also saves at fully admitted input-file boundaries
when a checkpoint destination is configured.
The [Protobuf configuration](protobuf-config.md) uses `checkpoint_file` and
`checkpoint_interval_jobs` for the same controls. Restore through the C++,
Python, or client/server checkpoint API.

## Client/server checkpoint requests

`SaveCheckpointRequest` returns the current simulation state as binary bytes;
`LoadCheckpointRequest` replaces the server's current state from compatible
checkpoint bytes. Both require a server built with `DR_EVT_WITH_SER20=ON`;
otherwise the server returns an error response. See the
[Client/Server Guide](../CLIENT_SERVER_GUIDE.md) for the request protocol.

## Output rollback and recovery

Buffered output is flushed before saving. Output files remain external to
the binary snapshot. For file output, the checkpoint stores each flushed byte
boundary. Loading it renames the current file to `<path>.pre-restart.N`, records the
boundary in an adjacent `.checkpoint-bytes` sidecar, and starts a fresh segment
at the original path.

After the resumed run, reconstruct each job or resource output with:

```bash
dr_evt_stitch_checkpoint_output output.csv output.stitched.csv
```

The tool discovers all numbered archives, retains each only through its saved
checkpoint boundary, removes repeated CSV headers, and atomically writes the
combined result.

Redis restart renames the canonical namespace to `PREFIX:pre-restart:N` and
starts a fresh canonical namespace. The Redis stitch command rebuilds its CSV
values, hashes, sets, and sorted indexes:

```bash
dr_evt_stitch_checkpoint_output --redis-uri URI --redis-prefix PREFIX
```

The Redis operation rebuilds the canonical namespace in place and removes the
consumed archived namespaces after it succeeds.

In both modes, rows written after the saved checkpoint remain in the archive
for auditability but are excluded by the recorded boundary during stitching.

These output limitations are unrelated to job-status queries.
[`get_job_statuses()`][cpp-simulation-get-job-statuses] works with Redis off or on, and its retained status
metadata is checkpoint-safe whenever checkpointing itself is available.

[cpp-sim-params]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/configuration.html#_CPPv4N6dr_evt10Sim_ParamsE
[cpp-simulation-advance-to]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4N6dr_evt15BasicSimulation10advance_toE10sim_time_t
[cpp-simulation-get-job-statuses]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation16get_job_statusesERKNSt6vectorI8job_no_tEE6size_tbb
[cpp-simulation-load-checkpoint]: https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4N6dr_evt15BasicSimulation15load_checkpointERKNSt6stringE
[cpp-simulation-restored-params]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4NK6dr_evt15BasicSimulation15restored_paramsEv
[cpp-simulation-save-checkpoint]: https://dr-evt.readthedocs.io/en/latest/api/cpp/source/sim.html#_CPPv4N6dr_evt15BasicSimulation15save_checkpointERKNSt6stringE
