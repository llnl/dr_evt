# Standalone count-model measurements

These reports describe the first count-model snapshot, before the later
[reservation-record optimization](reservation-record-elision.md). Their source
hashes identify that earlier snapshot rather than the current working tree.

The first count-model milestone meets the proposed 2% wall-time regression
threshold on these six workloads: every upper bound of the paired bootstrap
interval is below 2%. Keep count as a standalone model; these results do not
justify a separate count-specific scheduler traversal.

The baseline is revision `d3794773563c0f92c9c1b05d294bd5f505a93314`. Candidates
contain the uncommitted count-model extraction; the reports identify them with
binary hashes and hashes of the changed source/build files. All variants use
GCC 13.3.0, Linux arm64 Docker on an Apple M3 Max, Release (`-O3 -g`), static
DR_EVT libraries, and no LTO. Serialization, Python, Protobuf, and gRPC are
disabled for this experiment.

The four mixed cases have 2,000 jobs and nine measured samples, each averaging
20 executions after a warmup batch. The per-pass stress case has 8,000 jobs;
the existing cached conservative profile has 20,000 jobs. Stress cases use one
warmup followed by nine individually measured runs. Each variant's execution
order rotates between samples. Timings include process startup, input, simulation,
and schedule/resource CSV output; hashing and audits occur outside the timing.

| Case | Count / baseline paired median change | Bootstrap 95% interval | Hooks / count paired median change |
| --- | ---: | --- | ---: |
| mixed-none | -0.82% | -1.84% to +0.55% | +0.68% |
| mixed-easy | +0.54% | -1.02% to +1.37% | +3.15% |
| mixed-hybrid | -1.34% | -2.37% to +0.25% | +2.69% |
| mixed-conservative | +0.09% | -1.78% to +1.34% | +1.70% |
| stress-per-pass-conservative | -0.94% | -1.65% to +0.56% | +5.39% |
| stress-cached-conservative | -0.38% | -1.86% to +0.09% | -0.37% |

Negative changes do not establish a speedup. The measured count-model changes
are small relative to variation and code layout. Enabled diagnostic hooks have
measurable cost, reaching a paired median increase of 5.39% in the per-pass
stress case (95% interval 4.61%–7.89%). They remain disabled by default.
The cached scheduler's resource calls have not yet been migrated to CountModel;
that case measures the shared timeline extraction and committed event accounting,
and its hooks cover committed accounting rather than its cached fit searches.

## Correctness and generated code

- Every measured execution produced byte-identical schedule and resource-history
  hashes across all three variants. Independent audits checked job counts,
  submit/start ordering, execution durations, and aggregate capacity.
- Eight relevant native executables passed in both candidate configurations:
  count-model oracle/hooks, placement-pass accounting, OpenPBS forecast, capacity
  schedules, release delays, predictions, warm starts, and replay reclamation.
  The placement-pass integration driver also passed in both configurations.
- The 34 scheduler golden fixtures, 37 release-delay checks, and full
  resource-history suite passed on the final hooks-disabled candidate.
- The count timeline algorithm is an unchanged extraction of the baseline
  implementation. State/allocation remain scalar node counts and placement is
  a scalar start time; the existing job records retain demand and duration.
- GCC emits the same scalar load/add/store or load/subtract/store instructions
  for raw arithmetic and CountModel allocation/release. The production simulator
  contains no CountingResourceObserver symbols. The migrated per-pass traversal
  is 4,708 bytes in both baseline and final candidate; scalar placement avoids
  translating the original optional start result into a separate optional record.

These results cover one compiler, machine, and synthetic workload set. They do
not establish a universal zero-overhead guarantee or validate future identity,
multi-resource, or Fluxion models. Repeat the affected comparison when those
interfaces or scheduler paths change.

## Evidence and reproduction

- [Batched mixed-case report](batched-comparison.json)
- [Stress-case report](stress-comparison.json)
- [Build and benchmark instructions](../README.md#standalone-count-model-comparison)
