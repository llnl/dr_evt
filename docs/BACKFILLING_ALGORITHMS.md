# Scheduling Policies

DR_EVT separates queue ordering from the rule that permits a lower-priority
job to start ahead of the queue head. Command-line names and defaults are in
[Command-Line Options](user-guide/command-line.md#scheduling-policies).

A scheduling configuration is a set of policies and limits: queue ordering,
backfill protection, scheduling triggers, examination and reservation depths,
planning horizon and resolution, and reservation admission. These describe
the scheduling approach studied by the simulator. Their relationships to
publicly documented production behavior and the limits of those comparisons
are discussed in [Scheduling Loops](user-guide/SCHEDULER_LOOPS.md) and
[Realism and Known Gaps](user-guide/REALISM_AND_GAPS.md).

## Priority policies

- **FCFS** orders jobs by submission time and preserves input order for ties.
- **SJF** orders jobs by ascending estimated run time.
- **LJF** orders jobs by descending estimated run time.

The `fcfs_alt` policy is an alternative FCFS implementation used for
differential testing. `fcfs_conservative` selects the FCFS implementation that
maintains the reservations needed for conservative backfilling.

## Backfilling policies

At a permitted scheduling pass, start fitting jobs from the front of the
eligible queue. If the new head is blocked, the backfilling policy determines
whether a later job may start. Trigger modes and option combinations are in
[Scheduling Loops](user-guide/SCHEDULER_LOOPS.md).

### None

No job may bypass the blocked queue head.

### EASY

EASY protects the blocked head's reservation:

1. Starting with the currently free nodes, inspect the estimated completion
   times of running jobs until enough nodes would be available for the head.
   That time is the head's reservation.
2. Scan later eligible jobs in priority order.
3. Start a candidate only if it fits now and preserves the head's protected
   resource-time interval. With `DR_EVT_LEGACY_RESERVATION_BARRIER=ON`, require
   its estimated completion to be strictly before the protected start.

Only the queue head receives a reservation, so jobs deeper in the queue can be
delayed by backfilling.

### Experimental external selection

The C++ and Python APIs can opt into a circular-buffer EASY scheduler that
separates feasibility from selection. Construct [`Simulation`][cpp-simulation] with a candidate
limit, a job-cost function, and a selection function:

```cpp
params.m_num_max_candidates = 16;
auto cost_from_job_order =
    [](job_no_t job_id, sim_time_t, tdiff_t, num_nodes_t) {
      return static_cast<job_cost_t>(job_id);
    };
auto select_lowest_cost =
    [](const backfill_candidates_t& candidates) -> std::optional<job_no_t> {
      if (candidates.empty()) {
        return std::nullopt;
      }
      return std::min_element(
          candidates.begin(), candidates.end(),
          [](const auto& lhs, const auto& rhs) {
            return lhs.second < rhs.second;
          })->first;
    };

Simulation sim(params, cost_from_job_order, select_lowest_cost);
```

The cost function runs when each job enters the wait queue and its result is
stored as [`JobEntry::m_cost`][cpp-fcfseasycustomscheduler-jobentry]. When the FCFS head is blocked, the scheduler
collects up to the configured number of feasible candidates in FCFS order and
passes their `(job_id, cost)` pairs to the selector. Returning an ID not in the
offered vector is an error. Returning `std::nullopt` skips backfilling for that
scheduling decision. The example's cost-only `std::min_element` comparison
selects the first candidate in vector order when multiple costs are equal.

`num_max_candidates` is not a wait-queue scan limit. It applies only after the
custom EASY scheduler has tested jobs for resource and reservation-window
feasibility, and it limits the feasible jobs offered to the callback. In
contrast, `backfill_queue_depth` bounds how many waiting jobs a scheduler
(EASY, conservative, hybrid, or the custom EASY selector) examines before
feasibility testing. They are separate options that act at different stages:

```text
built-in schedulers:            waiting queue → backfill_queue_depth → feasibility
custom EASY selector:           waiting queue → backfill_queue_depth → feasibility
                               → num_max_candidates → callback selection
```

The callback scheduler currently uses EASY reservations; it does not implement
hybrid or conservative reservations.

### Conservative

Conservative backfilling protects every earlier waiting job with a chained
resource-time reservation plan. Each protected waiting job is placed in queue
order and consumes its requested nodes over its requested runtime before the
next protected job is planned. A candidate starts only if it fits now and its
projected execution does not delay any interval in that accumulated plan.
Jobs accepted earlier in the same scheduling pass are included when later
candidates are evaluated.

Use `--priority_policy fcfs_conservative --backfill_policy conservative` for
the full FCFS conservative-reservation implementation. It currently uses the
`circular` wait queue by default; use `--queue_impl deque` to select the
deque implementation.

### Hybrid

`--backfill_policy hybrid --reservation_depth N` builds that chained plan for
the first `N` waiting jobs in queue order. A candidate deeper than `N` may
start only if it does not delay any reservation in that plan; it holds no
reservation itself, so later candidates may pass it.
`N = 1` provides head protection; a depth covering all eligible waiting jobs
provides conservative protection. Hybrid and conservative accept `fcfs` or
`fcfs_conservative` ordering. The Python oracle
`scripts/python_hybrid_scheduler.py` accepts `--reservation-depth` and
`--backfill-queue-depth`.

Out-of-horizon jobs receive no protection and consume no reservation-depth
slot. Queue-depth accounting depends on the trigger mode; see
[Command-Line Options](user-guide/command-line.md).
Head protection alone does not guarantee identical EASY and hybrid results
with finite scan limits; see
[Backfill evaluation accounting](user-guide/REALISM_AND_GAPS.md#backfill-evaluation-accounting).

## Runtime estimates and completion

Reservations and backfill checks use each job's requested `time_limit`, which
is the information available to the scheduler. A simulation job's observed
run time may be shorter, depending on `--run_time_mode`. An early completion
releases its nodes (after any configured release delay); reconsideration follows
the selected trigger mode and does not change an earlier reservation calculation.

The default admission rule preserves protected resource-time intervals and can
allow overlapping jobs when enough resources remain. The legacy barrier requires
completion strictly before the protected start; see
[Reservation admission scope](user-guide/REALISM_AND_GAPS.md)
for the limits of this homogeneous-pool rule.

## Comparison

| Policy | Reservations protected | Main trade-off |
|---|---:|---|
| None | Queue order only | No backfill utilization benefit |
| EASY | Blocked queue head | More backfill opportunities; no guarantee for deeper jobs |
| Hybrid | First N waiting jobs | Tunable between the two; jobs beyond N are unprotected |
| Conservative | All earlier waiting jobs | Stronger start-time protection; more scheduling work and fewer opportunities |

The priority policy determines which job is considered "earlier" in this
table. Queue-storage implementations do not change the intended scheduling
result; see [Wait Queues](dev/WAIT_QUEUES.md).

## References

- Lifka, D. A. (1995), *The ANL/IBM SP Scheduling System*.
- Mu'alem, A. W., and Feitelson, D. G. (2001), *Utilization,
  Predictability, Workloads, and User Runtime Estimates in Scheduling the IBM
  SP2 with Backfilling*.
- Feitelson, D. G., and Weil, A. M. (1998), *Utilization and Predictability in
  Scheduling the IBM SP2 with Backfilling*.

Production scheduler sources and option mappings are in
[Scheduling Loops](user-guide/SCHEDULER_LOOPS.md#sources); accounting and modeling
limitations are in [Realism and Known Gaps](user-guide/REALISM_AND_GAPS.md).

[cpp-fcfseasycustomscheduler-jobentry]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/scheduling-backfill.html#_CPPv4N6dr_evt23FCFSEasyCustomScheduler8JobEntryE
[cpp-simulation]: https://dr-evt.readthedocs.io/en/latest/api/cpp/topics/simulator.html#_CPPv4I0EN6dr_evt15BasicSimulationE
