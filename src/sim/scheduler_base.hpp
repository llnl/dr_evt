/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_BASE_HPP
#define DR_EVT_SIM_SCHEDULER_BASE_HPP

#include <vector>
#include <map>
#include <memory>
#include "common.hpp"
#include "trace/job_record.hpp"
#include "trace/trace.hpp"
#include "sim/scheduler_policies.hpp"
#include "params/sim_params.hpp"

namespace dr_evt {

/**
 * Abstract base class for job schedulers.
 * Different implementations for FCFS vs priority-based scheduling.
 */
class SchedulerBase {
protected:
    /// Total nodes available to jobs selected by this scheduler.
    num_nodes_t m_total_nodes;
    /// Backfill rule applied when jobs other than the FCFS head are considered.
    BackfillPolicy m_backfill_policy;
    /// Raw pointer to the owning Trace, not the job-record container
    /// directly: reading a job's data by job_no needs Trace::job_at()'s
    /// translation (job_no - m_num_reclaimed), which only Trace can do -
    /// m_data's own physical layout shifts as reclaiming advances.
    const Trace* m_trace_ptr;
    /// Projected earliest start time reserved for the FCFS queue head.
    sim_time_t m_fcfs_reservation_time;

public:
    /**
     * @brief Construct a scheduler operating on an owning simulation trace.
     * @param[in] total_nodes Total nodes available to schedule.
     * @param[in] trace Trace containing the jobs identified by insert_job().
     * @param[in] bf_policy Backfilling policy to apply during scheduling.
     */
    SchedulerBase(num_nodes_t total_nodes,
                  const Trace& trace,
                  BackfillPolicy bf_policy)
        : m_total_nodes(total_nodes)
        , m_backfill_policy(bf_policy)
        , m_trace_ptr(&trace)
        , m_fcfs_reservation_time(0)
    {}

    /** @brief Destroy a scheduler through its polymorphic base interface. */
    virtual ~SchedulerBase() = default;

    /**
     * @brief Enqueue an already-validated job in this scheduler's wait queue.
     * @details
     * This does not read or create a Trace job record and does not start the
     * job. Simulation::submit_job() supplies the copied scheduling fields;
     * a subsequent schedule() call chooses any job that can start.
     *
     * @param[in] job_id Existing Trace job identifier.
     * @param[in] submit_time Arrival time used for eligibility and ordering.
     * @param[in] run_time_estimate Time-limit estimate used for reservations.
     * @param[in] nodes_requested Requested node count.
     * @see Simulation::submit_job()
     * @see Trace::insert_job()
     */
    virtual void insert_job(job_no_t job_id, sim_time_t submit_time,
                           tdiff_t run_time_estimate, num_nodes_t nodes_requested) = 0;

    /**
     * @brief Select wait-queue jobs that may start at current_time.
     * @details
     * The returned identifiers are still existing Trace records. The caller
     * records each selected start through Trace::insert_job().
     *
     * @param[in] free_nodes Nodes currently available to allocate.
     * @param[in] running_jobs Running job identifiers and their start times.
     * @param[in] current_time Simulation time at which eligibility is evaluated.
     * @return Vector of existing Trace job identifiers selected to start.
     * @see insert_job()
     * @see Trace::insert_job()
     */
    virtual std::vector<job_no_t> schedule(
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time) = 0;

    /**
     * @brief Advance this scheduler's internal eligibility tracking.
     * @details
     * This is an explicit command that advances eligibility tracking to
     * current_time. Idempotent and cheap to call redundantly
     * (implementations early-return if current_time hasn't advanced past
     * what's already tracked) - schedule() calls this at its own top
     * unconditionally, so it remains safe to call regardless of whether
     * a caller has already synced.
     *
     * This is the ONLY place eligibility tracking advances. The query
     * methods below (active_job_count, get_next_arrival_time,
     * has_eligible_jobs) do NOT sync themselves - they trust that
     * whoever cares about a particular current_time has already called
     * sync_to(current_time) first. Simulation::advance_to() calls this
     * explicitly immediately after each of its two m_current_time
     * assignments, closing the one gap where schedule() itself might not
     * run in the same step (see Simulation::advance_to() for the traced
     * justification).
     */
    virtual void sync_to(sim_time_t current_time) = 0;

    /**
     * @brief Count jobs that have arrived but remain unscheduled.
     * @details
     * Counts jobs that are WAITING to be scheduled (arrived but not yet
     * scheduled), as of whatever time this scheduler was last synced to
     * via sync_to(). Does not take a time parameter and does not sync
     * itself - see sync_to() above for why that's safe here.
     *
     * @return Number of waiting jobs (arrived but not yet scheduled).
     */
    virtual size_t active_job_count() = 0;

    /**
     * @brief Return the earliest future arrival known to this scheduler.
     * @details
     * Call sync_to() first. Returns std::numeric_limits<sim_time_t>::max()
     * when no future arrival remains.
     * @return Earliest future arrival as sim_time_t, or the maximum value.
     */
    virtual sim_time_t get_next_arrival_time() = 0;

    /**
     * @brief Report whether at least one already-arrived job is waiting.
     * @details
     * Call sync_to() first; this query does not update eligibility itself.
     * @return true when at least one eligible, unscheduled job exists.
     */
    virtual bool has_eligible_jobs() = 0;

    /**
     * @brief Return the current FCFS-head reservation time.
     * @return Earliest projected time the FCFS head can start, in sim_time_t.
     */
    sim_time_t get_fcfs_reservation_time() const {
        return m_fcfs_reservation_time;
    }

protected:
    /**
     * Return total size of the wait queue (all jobs, ALL states).
     *
     * Counts ALL jobs in the queue:
     * - Future arrivals (submit_time > current_time)
     * - Waiting jobs (submit_time <= current_time AND !removed)
     * - Scheduled jobs (submit_time <= current_time AND removed)
     *
     * Internal utility only - no external caller needs the "including
     * scheduled jobs" count. Use active_job_count() for the number of
     * jobs actually waiting to be scheduled.
     *
     * @return Total jobs in the wait queue as size_t, including removed entries.
     */
    virtual size_t wait_queue_size() const = 0;

    /**
     * @brief Look up the time-limit estimate used for reservation planning.
     * @param[in] job_idx Identifier of an existing trace job.
     * @return The job's requested time limit, in tdiff_t.
     */
    tdiff_t get_duration_estimate(job_no_t job_idx) const {
        // Scheduler uses time_limit as the best estimator for planning (realistic mode)
        const auto& job = m_trace_ptr->job_at(job_idx);
        return job.get_limit_time();
    }

    /**
     * @brief Project when the first FCFS job can obtain its requested nodes.
     * @details
     * Starts with currently free nodes and applies estimated release times
     * for running jobs until the FCFS head's request can be satisfied. The
     * result is used both for the head reservation and to decide whether a
     * backfill candidate can run without delaying that reservation.
     * @param[in] nodes_needed Nodes requested by that job.
     * @param[in] free_nodes Nodes available at current_time.
     * @param[in] running_jobs Active jobs and their start times.
     * @param[in] current_time Time from which to project releases.
     * @return Earliest projected start time for the FCFS head, in sim_time_t.
     */
    sim_time_t calculate_fcfs_reservation(
        num_nodes_t nodes_needed,
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time);
};

/**
 * @brief Construct the scheduler selected by the configured policies.
 * @details
 * Selects an FCFS implementation when FCFS is requested (including the
 * queue implementation and overflow settings), the conservative FCFS
 * implementation when required by the backfill policy, or the SJF/LJF
 * priority scheduler. The returned object retains a non-owning reference to
 * job_data, which must outlive the scheduler.
 * @param[in] total_nodes Total nodes available to schedule.
 * @param[in] job_data Trace whose records are scheduled.
 * @param[in] backfill_policy Backfilling policy.
 * @param[in] priority_policy Job-selection policy.
 * @param[in] queue_impl Wait-queue implementation for FCFS scheduling.
 * @param[in] block_size Block size when queue_impl selects a block queue.
 * @param[in] wait_queue_capacity Initial circular-queue capacity; zero selects a default.
 * @param[in] wait_queue_overflow Behavior when a circular queue is full.
 * @return Owning pointer to a SchedulerBase implementation.
 */
std::unique_ptr<SchedulerBase> create_scheduler(
    num_nodes_t total_nodes,
    const Trace& job_data,
    BackfillPolicy backfill_policy,
    PriorityPolicy priority_policy,
    QueueImplementation queue_impl = QueueImplementation::CIRCULAR,
    size_t block_size = 128,
    size_t wait_queue_capacity = 0,
    CircularOverflowPolicy wait_queue_overflow = CircularOverflowPolicy::GROW);

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_BASE_HPP
