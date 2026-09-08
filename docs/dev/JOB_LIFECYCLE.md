# Job Lifecycle

The names `submit_job()` and `insert_job()` describe different transitions.
This page is the development-level companion to their Doxygen comments.

```text
Simulation::append_job()
  -> Trace::append_job()             create a Job_Record in the job store
  -> Simulation::submit_job()        validate arrival and collect accounting
     -> SchedulerBase::insert_job()  add scheduling data to the wait queue

Simulation::advance_to()
  -> SchedulerBase::schedule()       select eligible wait-queue job(s)
  -> Trace::insert_job()             record start/end events for each start
```

`SchedulerBase::insert_job()` and `Trace::insert_job()` deliberately operate
on different structures despite their shared name. The former is wait-queue
insertion; the latter records that a selected job has started. Neither creates
a new `Job_Record`: only `Trace::append_job()` does that for live arrivals.

For a batch-loaded trace, `Simulation::run()` invokes the protected
`Simulation::submit_job()` helper for each already-loaded record before it
advances time. Public streaming callers use `append_job()` or `append_jobs()`;
those APIs perform the create-and-enqueue transition atomically.
