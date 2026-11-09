# Resource Model

DR_EVT uses aggregate resource counts to keep scheduler simulation efficient
enough to simulate multiple clusters simultaneously. A resource-model plug-in
hook does not exist yet. Future work is to create that hook, then implement a
bitmap-based resource model that can track individual resources.

## Why resource-model cost matters

Even a single-cluster simulation can require substantial computation: each
scheduling pass examines waiting jobs and searches future capacity to build
reservations. Repeating those decisions over a long trace makes both
computation and memory use important. Simulating several clusters multiplies
that work and the state held for each system.

The [online multi-cluster experiment](../../user-guide/multi-cluster-experiment.md)
uses one independent simulation per cluster and coordinates workload placement
across their current queue states. The resource model must leave enough
computational capacity for those simulations and the dispatcher.

## Current decision: aggregate counts

The built-in resource model treats a cluster as a homogeneous pool of
interchangeable nodes. Jobs request a node count; running jobs consume counts,
and reservations protect counts over future time intervals. Capacity changes
and node-release delays update that same pool.

This avoids storing and searching a resource graph or maintaining a specific
node assignment for every allocation and reservation. It reduces the amount
of state and placement work needed per job and per scheduling pass. The
trade-off is that sufficient aggregate capacity does not reproduce a native
scheduler's choice of particular nodes or its reservations on those nodes.

## Observed impact in validation

The [Flux validation](../../user-guide/FLUX_VALIDATION.md) compares DR_EVT with
Fiction driving native Flux schedulers. Its hybrid-backfill comparison uses
Fluxion, which matches concrete resources and reserves particular nodes;
DR_EVT reserves aggregate capacity.

The [four-job reproducer](../../user-guide/FLUX_VALIDATION.md#minimal-reproducer-for-backfill-start-time-differences)
isolates this distinction. Fluxion's low-node-ID-first matcher reserves the
nodes that become free at 70 seconds for an earlier waiting job at 80 seconds.
It therefore delays a later job until 80 seconds. DR_EVT can start that job at
70 seconds while preserving enough aggregate capacity for the earlier job
at 80 seconds. A feasible physical assignment exists, but it differs from
Fluxion's selected reservation placement.

This demonstrates a scheduling consequence of the simplified model. It does
not establish the cause of every difference in the larger workload; the
validation page records the broader comparison and evidence limits.

## Planned extension

The first step is to design and implement a resource-model hook so a simulation
can select an alternative allocation and reservation model. The aggregate-count
model will remain available for studies that prioritize simulation throughput.

The planned bitmap-based model will represent resource identities with bits
and track allocation and reservation membership. This offers a compact path
to modeling concrete resource placement. Matching and reservation rules still
need to be defined and validated; a bitmap alone does not reproduce Fluxion's
full resource graph or policy. The hook and bitmap model are future work,
rather than currently available configuration options.
