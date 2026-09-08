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
    num_nodes_t m_total_nodes;
    BackfillPolicy m_backfill_policy;
    /// Raw pointer to the owning Trace, not the job-record container
    /// directly: reading a job's data by job_no needs Trace::job_at()'s
    /// translation (job_no - m_num_reclaimed), which only Trace can do -
    /// m_data's own physical layout shifts as reclaiming advances.
    const Trace* m_trace_ptr;
    sim_time_t m_fcfs_reservation_time;

public:
    SchedulerBase(num_nodes_t total_nodes,
                  const Trace& trace,
                  BackfillPolicy bf_policy)
        : m_total_nodes(total_nodes)
        , m_backfill_policy(bf_policy)
        , m_trace_ptr(&trace)
        , m_fcfs_reservation_time(0)
    {}

    virtual ~SchedulerBase() = default;

    /**
     * @brief Enqueue an already-validated job in this scheduler's wait queue.
     *
     * This does not read or create a Trace job record and does not start the
     * job. Simulation::submit_job() supplies the copied scheduling fields;
     * a subsequent schedule() call chooses any job that can start.
     *
     * @param job_id Existing Trace job identifier
     * @param submit_time Arrival time used for eligibility and ordering
     * @param run_time_estimate Time-limit estimate used for reservations
     * @param nodes_requested Requested node count
     * @see Simulation::submit_job()
     * @see Trace::insert_job()
     */
    virtual void insert_job(job_no_t job_id, sim_time_t submit_time,
                           tdiff_t run_time_estimate, num_nodes_t nodes_requested) = 0;

    /**
     * @brief Select wait-queue jobs that may start at current_time.
     *
     * The returned identifiers are still existing Trace records. The caller
     * records each selected start through Trace::insert_job().
     *
     * @param free_nodes Nodes currently available to allocate
     * @param running_jobs Running job identifiers and their start times
     * @param current_time Simulation time at which eligibility is evaluated
     * @return Identifiers of jobs selected to start
     * @see insert_job()
     * @see Trace::insert_job()
     */
    virtual std::vector<job_no_t> schedule(
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time) = 0;

    /**
     * Explicit command: advance this scheduler's internal eligibility
     * tracking to current_time. Idempotent and cheap to call redundantly
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
     * Count jobs that are WAITING to be scheduled (arrived but not yet
     * scheduled), as of whatever time this scheduler was last synced to
     * via sync_to(). Does not take a time parameter and does not sync
     * itself - see sync_to() above for why that's safe here.
     *
     * @return Number of waiting jobs (arrived but not yet scheduled)
     */
    virtual size_t active_job_count() = 0;

    // Query methods for advance_to() logic - see sync_to() above for why
    // these don't take a time parameter or sync themselves.
    virtual sim_time_t get_next_arrival_time() = 0;
    virtual bool has_eligible_jobs() = 0;

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
     * @return Total jobs in wait queue (all states)
     */
    virtual size_t wait_queue_size() const = 0;

    tdiff_t get_duration_estimate(job_no_t job_idx) const {
        // Scheduler uses time_limit as the best estimator for planning (realistic mode)
        const auto& job = m_trace_ptr->job_at(job_idx);
        return job.get_limit_time();
    }

    sim_time_t calculate_fcfs_reservation(
        num_nodes_t nodes_needed,
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time);
};

/**
 * Factory function to create appropriate scheduler based on priority policy
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
