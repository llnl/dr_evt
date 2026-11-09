# Inlining cached-start recording

Inlining `record_planned_start()` improves the bounded hybrid workload by approximately **2%** relative to the initial cached-start implementation. Its remaining overhead against the pre-feature baseline is approximately **2.4%**. EASY is unchanged within run-to-run variation.

## Measurement

Same native Release libraries and workload as the original report: 50 complete 10,000-job simulations per process, circular queue, 795 nodes, Q=64, R=32, requested-limit runtimes. One warmup per version/policy and five measured repetitions, alternating order. Builds finish before measurement. All processes start exactly 500,000 jobs.

Three libraries are compared in the same session: committed pre-feature baseline, initial cached-start implementation, and forced-inline cached-start implementation. `DYLD_LIBRARY_PATH` selects the exact matching library directory for each process. Library hashes and individual runs are recorded in `inline.json`.

| Policy | Pre-feature baseline (s) | Initial cached API (s) | Inline cached API (s) | Improvement over initial | Remaining overhead over baseline |
| --- | ---: | ---: | ---: | ---: | ---: |
| easy | 1.5451 | 1.5453 | 1.5434 | 0.12% | -0.11% |
| hybrid | 4.2378 | 4.4312 | 4.3407 | 2.04% | +2.43% |

Hybrid user CPU also decreases, from 4.3492 s to 4.2612 s (2.02%). The candidate preserves cache layout, optional timestamps, prediction kinds, checkpoint behavior, and query semantics. It introduces no additional resource searches or per-job queue fields.

## Change retained

The working tree marks the recording helper `[[gnu::always_inline]] inline`, matching the attribute already used for the shared hybrid scheduling helper. This removes the out-of-line helper symbol from the optimized library. The Mach-O text segment grows by 16 KiB (about 1.3%); the cache layout is unchanged. The experiment only changes that declaration; a short explanatory comment is included in the working-tree version.

## Further opportunities

- Avoid recording provisional `ESTIMATED` values that are immediately replaced by protected `RESERVED` values in the same pass, or belong to candidates selected to start immediately. This requires preserving early-return and failed-protection behavior; it has not been implemented or measured.
- Partition query results into known and unknown starts, then sort only the known portion while preserving unknown queue order. This may help large queues with shallow examination depth. It has not been implemented or measured.
- Keep these as separate experiments: the profile supports the inline change, but does not establish the benefit of the other candidates.

## Evidence

`inline.json` contains all measurements and library hashes. `inline_compare.py`, `inline-change.diff.gz`, and `inline-build.log` describe the experiment. The original cached-API binaries are preserved in `/private/tmp/dr_evt-plan-profile-20261109/after-original`; the optimized library is in `after-build`. The original report and original raw measurements remain unchanged.
## Validation

All eight selected regressions pass: cached starts in C++ and Python, append/status API, checkpoint restart, custom scheduler, placement-pass accounting, OpenPBS forecast, and capacity scheduling. `git diff --check` passes.
