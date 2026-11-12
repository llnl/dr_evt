# Fluxion placement oracle

[placement_oracle.py](placement_oracle.py) uses native Fluxion, driven by Fiction,
to capture whole-node placement decisions. The reference is Fluxion's resource
matcher and queue policy. No DR_EVT scheduler or capacity-model schedule is used
to generate expected results.

## Reference conditions

The initial oracle has four interchangeable nodes with one core each, exclusive
whole-node requests, equal job priorities, one unrestricted queue, and an empty
initial cluster. Input order resolves equal-time submissions. Requested limits
control forecasts; actual runtimes control completions. Jobs do not exceed their
limits. Release delay and simulated system latency are zero, and Fiction waits
for the native scheduler to become quiescent before advancing virtual time.

The default capture runs six workloads with both `lonodex` (low-ID-first) and
`hinodex` (high-ID-first), under FCFS, EASY, HYBRID R=1, HYBRID R=2, and
conservative policies: 60 cases. Q=2048 covers each complete workload so queue-cap
restarts do not confound placement. Fluxion's `first` matcher can be selected
explicitly; it follows native traversal order and is not equated with low-ID-first.

| Workload | Behavior exercised |
| --- | --- |
| Identity reservation | Four-job example with D starting at 80 under backfill |
| Swapped input order | Different initial identities allow D to start at 70 |
| Backfill before reservation | A later job finishes before the protected interval |
| Exact release boundary | Release and reservation start at the same timestamp |
| Two reservations | Multiple future intervals and hybrid depth limits |
| Early completion | Actual release precedes the forecast based on the limit |

## Fixture contract

The checked [placement-oracle.json](evidence/placement-oracle.json) has schema
version 1. Its `reference` records native source revisions, synchronization
overlay identifiers, runtime binary hashes, and capture-script hashes.

Each case records its matching/queue policy, reservation depth, pool size, queue
cap, and input `jobs` as `[submit, nodes, time_limit, actual_runtime]`. Jobs are
identified by their input index, avoiding run-dependent Flux job IDs.

- `schedule` records each job's actual `start`, `finish`, and sorted `node_ids`.
- `snapshots` records native allocations and reservations at each quiescent
  boundary. Reservation intervals use the native resource module's scheduled
  start and the job's requested limit; they do not use its actual finish.
- Times are offsets from the harness epoch. Intervals are half-open.
- `raw_sha256` identifies the native transitions, resource snapshots, and run
  configuration. Full logs and configurations remain in the capture directory.

The read-only observer queries `sched-fluxion-resource.find` for allocated and
reserved identities and `sched-fluxion-resource.info` for reserved start times.
It does not allocate, reserve, cancel, or request additional scheduling passes.
These are boundary snapshots; transient reservations inside one queue traversal
are not observed. A future reservation may change after an early completion or
another native pass, and its reserved identities need not equal its eventual
allocation.

`audit_case()` checks execution duration and submission ordering, per-node
exclusivity, exact ownership at snapshots, complete release, reservation depth,
and non-overlapping reserved intervals. Reservations must fit around currently
running jobs' forecast intervals. The audits reject wrong identities even when
aggregate resource counts remain correct. They check validity of the capture;
Fluxion supplies the expected scheduling and placement decisions.

The reusable C++ `PlacementModel` is compared with these exact starts, finishes,
node IDs, and reservation snapshots by `compare_placement_model.py`, which
runs both coarse-filter and exact-only compile-time specializations. The
validation-only driver groups arrivals and completions at each timestamp,
rebuilds the pass-local plan from borrowed running allocations, and uses the
same queue policies and reservation limits. Each job owns its live bitmap until
completion; speculative future masks belong to the planner. This driver is
separate from simulator CLI integration. CapacityModel schedules are not an equality target for this oracle.

## Generate and verify

In the prepared validation container, with `FLUX_FICTION_JOBTAP_SO` pointing to
its built emulator jobtap:

```sh
flux env python3 /validation/placement_oracle.py --outdir /results/placement-oracle
flux env python3 /validation/placement_oracle.py --policy hybrid --match-policy lonodex --case two_reservations --outdir /results/placement-depth
```

Each capture retains native inputs, transitions, snapshots, and logs. Audit a
fixture without Flux installed:

```sh
python3 validation/flux/placement_oracle.py --verify validation/flux/evidence/placement-oracle.json
python3 -m unittest discover -s validation/flux -p test_placement_oracle.py -v
```

This fixture is limited to the stated whole-node model. It does not validate
cores, GPUs, topology, node weights, resource sharing, or Slurm placement.

Compare the C++ model without installing Flux (after a normal CMake build):

```sh
./build/test_placement_model
python3 validation/flux/compare_placement_model.py ./build/test_flux_placement_driver
ctest --test-dir build -R 'test_(placement_model|flux_placement_model)$' --output-on-failure
```

The driver reads a narrow fixture protocol from stdin and emits JSON lines for
job starts and quiescent snapshots. It assumes unlimited queue examination and
the whole-node conditions above. The checked fixture's native decisions remain
the reference; the driver does not generate or rewrite expected results.

The driver's `--exact-only` argument selects the disabled template specialization
once before processing jobs. Both specializations use the same exact planner;
there is no per-query runtime filter switch. `compare_placement_model.py`
checks both automatically. This argument belongs only to the validation driver,
not the simulator CLI.
