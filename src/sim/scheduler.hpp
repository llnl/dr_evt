/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_HPP
#define DR_EVT_SIM_SCHEDULER_HPP

#include <deque>
#include <map>
#include <set>
#include <vector>
#include "common.hpp"
#include "trace/job_record.hpp"
#include "sim/scheduler_policies.hpp"

namespace dr_evt {

/**
 * Stateless job scheduler - makes scheduling decisions without maintaining state
 *
 * The scheduler is a pure decision engine that takes current state as input
 * and returns which jobs should be fed to the replay engine.
 *
 * State is maintained by:
 * - wait_queue: Jobs waiting to be scheduled (managed by Simulation)
 * - Replay engine (Trace::Context): Running jobs and resource allocation
 *
 * Usage:
 * @code
 * // On job arrival or completion:
 * auto jobs_to_run = scheduler.schedule(
 *     wait_queue,        // Jobs waiting
 *     free_nodes,        // Current free nodes
 *     running_jobs,      // Currently running jobs (from replay context)
 *     job_data,          // Job details
 *     current_time       // Current sim time
 * );
 *
 * // Feed returned jobs to replay engine:
 * for (job_idx : jobs_to_run) {
 *     trace.insert_job(job_idx, current_time, context);
 * }
 * @endcode
 */
class Scheduler {
  protected:
    num_nodes_t m_total_nodes;
    BackfillPolicy m_backfill_policy;
    PriorityPolicy m_priority_policy;
    RuntimeEstimateMode m_runtime_mode;

    const std::vector<Job_Record>* m_job_data_ptr;

    // FCFS reservation time - for backfill window calculation only
    sim_time_t m_fcfs_reservation_time;

  public:
    /**
     * Constructor
     * @param total_nodes Total nodes in system
     * @param job_data Reference to job trace data
     * @param bf_policy Backfill policy
     * @param pri_policy Priority policy
     * @param rt_mode Runtime estimate mode
     */
    Scheduler(num_nodes_t total_nodes,
              const std::vector<Job_Record>& job_data,
              BackfillPolicy bf_policy = BackfillPolicy::EASY,
              PriorityPolicy pri_policy = PriorityPolicy::FCFS,
              RuntimeEstimateMode rt_mode = RuntimeEstimateMode::USE_LIMIT);

    /**
     * Main scheduling decision function
     * Called on job arrival or job completion
     *
     * @param wait_queue Jobs waiting (pairs of job_idx, removed_flag). Scheduled jobs marked as removed.
     * @param free_nodes Currently available nodes
     * @param running_jobs Map of currently running jobs (job_idx -> start_time)
     * @param current_time Current simulation time
     * @return List of jobs that should be fed to replay engine
     */
    std::vector<job_no_t> schedule(
        std::deque<std::pair<job_no_t, bool>>& wait_queue,
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time);

    /**
     * Get current FCFS reservation time (for debugging/stats)
     */
    sim_time_t get_fcfs_reservation_time() const {
        return m_fcfs_reservation_time;
    }

  protected:
    /**
     * Sort jobs according to priority policy
     */
    void apply_priority_policy(std::vector<job_no_t>& jobs) const;
    std::vector<job_no_t> sort_jobs(const std::set<job_no_t>& jobs) const;

    /**
     * Get runtime estimate for a job
     */
    tdiff_t get_runtime_estimate(job_no_t job_idx) const;

    /**
     * Calculate when FCFS head can start (based on running jobs completing)
     */
    sim_time_t calculate_fcfs_reservation(
        job_no_t job_idx,
        num_nodes_t free_nodes,
        const std::map<job_no_t, sim_time_t>& running_jobs,
        sim_time_t current_time);
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_HPP
