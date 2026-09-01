# simple_2jobs

## Scenario
Two jobs, both submitted at t=0, both requesting 60 nodes (out of 100
total) - only one can run at a time, forcing a genuine priority-policy
decision about which goes first. This is designed specifically to
demonstrate how FCFS, SJF, and LJF priority policies diverge on the same
input.

## Jobs
- Job 0: submit=0, nodes=60, time_limit=100 (long)
- Job 1: submit=0, nodes=60, time_limit=20 (short)

## Results by priority policy

**Note**: `run_unit_tests.sh` hardcodes `--priority_policy fcfs` for every
test in this directory, so only the FCFS result is what this specific
test's `.expected_output.csv`/`.expected_resources.csv` actually check.
The SJF and LJF results below are recorded here for reference/manual
verification - they demonstrate the scenario is meaningful, but are not
automatically re-checked by any test runner.

### FCFS (what this test's expected files check)
```
Job 0: [0, 100]    (goes first - earlier in file, same submit time)
Job 1: [100, 120]  (waits)
```

### SJF (Shortest Job First - by time_limit ascending)
```
Job 1: [0, 20]     (goes first - shorter time_limit, 20 < 100)
Job 0: [20, 120]   (waits)
```
Diverges from FCFS: SJF picks the short job first, changing which job
waits and for how long.

### LJF (Longest Job First - by time_limit descending)
```
Job 0: [0, 100]    (goes first - longer time_limit, 100 > 20)
Job 1: [100, 120]  (waits)
```
Matches FCFS here (coincidentally, since Job 0 is both first-in-file and
longest) - LJF and FCFS do not diverge on this particular input, only
SJF does. A three-way divergence isn't achievable with exactly 2 jobs,
since there are only 2 possible orderings but 3 policies being compared.
