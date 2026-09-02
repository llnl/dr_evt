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
    RuntimeEstimateMode m_runtime_mode;
    const std::vector<Job_Record>* m_job_data_ptr;
    sim_time_t m_fcfs_reservation_time;

public:
    SchedulerBase(num_nodes_t total_nodes,
                  const std::vector<Job_Record>& job_data,
                  BackfillPolicy bf_policy,
                  RuntimeEstimateMode rt_mode)
        : m_total_nodes(total_nodes)
        , m_backfill_policy(bf_policy)
        , m_runtime_mode(rt_mode)
        , m_job_data_ptr(&job_data)
        , m_fcfs_reservation_time(0)
    {}

    virtual ~SchedulerBase() = default;

    // Core interface - must be implemented by subclasses
    // STATEFUL: Each scheduler maintains its own internal wait_queue
    virtual void insert_job(job_no_t job_id, sim_time_t submit_time,
                           tdiff_t runtime_estimate, num_nodes_t nodes_requested) = 0;

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

    tdiff_t get_runtime_estimate(job_no_t job_idx) const {
        const auto& job = (*m_job_data_ptr)[job_idx];
        if (m_runtime_mode == RuntimeEstimateMode::USE_ACTUAL) {
            return job.get_actual_duration();
        }
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
    const std::vector<Job_Record>& job_data,
    BackfillPolicy backfill_policy,
    PriorityPolicy priority_policy,
    RuntimeEstimateMode runtime_mode,
    QueueImplementation queue_impl = QueueImplementation::DEQUE,
    size_t block_size = 128);

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_BASE_HPP
