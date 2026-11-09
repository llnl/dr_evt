# Native OpenPBS agreement with the separate DR_EVT policy

The opt-in `fcfs_1s_evt_group` policy reproduces the measured OpenPBS 23.06.06
future-event grouping while retaining exact capacity intervals and actual
arrival/completion timestamps. The native OpenPBS scheduler library remains
unmodified at revision `cd7ab5edaf03dcac2f1cb9e2f42d75eb117d468a`.

The saved replays used the earlier name `fcfs_openpbs`. Their commands,
source snapshots, and hashes retain that spelling. The implementation commit
`1abb5c1` names the same grouping behavior `fcfs_1s_evt_group` and selects
forecast query paths at compile time.

| Comparison | Jobs | Differing starts or completions |
|---|---:|---:|
| All 21 small cases, including the four-job adjacent-release case | 392 | 0 |
| Original converted Lassen prefix, R=32, capacity=756 | 2,000 | 0 |

These 22 comparisons passed capacity, private-vnode ownership, duration, and
resource-area audits. The original prefix uses exactly the same converted
input as the earlier comparison. No additional delay or time multiplication
is needed for its match. Both schedulers have mean wait 23,620.5925 seconds,
makespan 143,232 seconds, and utilization 75.0899% on that input.

The default `fcfs` policy retains its original behavior. Its earlier replay
has 1,433 differing schedules; see the
[original record](../whole-seconds-20261108/README.md). This record concerns a
separate policy, not a change to the default policy or native OpenPBS source.

## What the policy reproduces

An earlier waiting job's predicted start determines whether a later job may
use spare capacity now. OpenPBS predicts that start by examining a hypothetical
future during the current scheduling decision. With `opt_backfill_fuzzy=off`,
it groups predicted events through one second after the earliest remaining
event, including the endpoint, and checks capacity after the whole group.
The time checked is the last event actually processed. Completions at 10 and
11 are checked together at 11; completions at 10 and 12 are separate, with the
first check at 10. A lone completion at 10 is also checked at 10. Actual
scheduling-loop triggers remain at their original timestamps.

The DR_EVT policy uses that rule when placing future reservations and testing
backfill candidates. Integer-second inputs, private vnodes, fixed shared
capacity, FCFS ordering, R=32, and unlimited candidate examination define the
large comparison. Native server/MOM execution, node placement, fairshare,
cycle time limits, and other OpenPBS policies are outside its scope.

## Reproduce and inspect

From the repository root, with the pinned OpenPBS checkout and Lassen trace:

```sh
bash validation/openpbs/run.sh --priority_policy fcfs_1s_evt_group
```

The comparator requires exact agreement for all small cases and the original
large input when this policy is selected. [summary.json](summary.json) contains
the historical results, including an additional time-doubled diagnostic that
is excluded from the published comparison and current runner. That diagnostic
changes the workload and is not a fix. Each case has its native configuration,
commands, and report. The large replay directories contain their converted inputs and complete
schedules inside the reports. Full event streams and logs remain under the
ignored `results/development/openpbs-policy/` directory.

[dr_evt-build.json](dr_evt-build.json) identifies the actual simulator build
and its source hashes; [openpbs-build.json](openpbs-build.json) verifies all
63 native scheduler source/header files and records library, binary, adapter,
and generated resource-definition hashes. `sources/dr_evt/` preserves the
18 implementation/test/build files that differed from the recorded base
revision in the actual replay build. Those snapshots precede final formatting
and documentation changes and remain unchanged to preserve their hashes.
The exact native adapter and generated resource definitions are under
`sources/native-adapter/`. [source-evidence.json](source-evidence.json) lists
the preserved files and source directory.

See the [harness README](../../README.md) for the full rule, dependencies,
formatting, Doxygen, test commands, and CI coverage.

## Final checks

[ci-summary.json](ci-summary.json) records 95 successful workflow shell steps
across GCC 14, Clang 18, serialization-disabled builds, both Redis variants,
quick checks, and documentation. These were run locally in isolated container
workspaces; GitHub Actions publication and upload actions were not invoked.
The record identifies the tested source snapshot and local environment changes.
At the time of that run, production code, tests, and workflows matched that
snapshot. The validation documentation was rebuilt with warnings treated as errors after
its result text was updated. New C++ files and changed C++ ranges pass
clang-format 18 with LLVM style; API and adapter Doxygen checks passed.
Full logs are preserved under the ignored
`results/development/openpbs-policy/full-ci/` directory.

After the policy was renamed and the query paths were selected at compile
time, both GCC and Clang builds passed all configured CTests, 22 Python API
tests, and both gRPC tests. Additional Python checks verified the new enum
and both forecast rules. Documentation rebuilt with warnings treated as
errors. [query-checks.json](query-checks.json) records these checks for the
implementation in `1abb5c1`. The large native replays above used the preserved
earlier source snapshots; they were not repeated for the rename and query refactor.
