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

#include <set>
#include <map>
#include <vector>
#include "common.hpp"
#include "trace/trace.hpp"
#include "sim/scheduler_policies.hpp"
#include "sim/schedule_windows.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * @brief SLURM-style job scheduler with backfilling support.
 *
 * Implements EASY and Conservative backfilling algorithms based on
 * the ScheduleFlow implementation from ExaDigiT/RAPS.
 *
 * @section algorithms Backfilling Algorithms
 *
 * **EASY Backfill**:
 * - First job in queue gets guaranteed start time (reservation)
 * - Later jobs can backfill if they fit now and won't delay first job
 * - Simple, efficient, commonly used in production HPC centers
 *
 * **Conservative Backfill**:
 * - ALL queued jobs get guaranteed start times
 * - Backfilling jobs cannot delay ANY reservation
 * - More complex but provides guarantees to all jobs
 *
 * @section state Scheduler State
 *
 * The scheduler maintains:
 * - A wait queue of submitted but not yet running jobs
 * - A scheduled jobs map (job -> reservation time)
 * - A running jobs map (job -> actual start time)
 * - A schedule windows structure for tracking available resources
 *
 * @section usage Example Usage
 * @code
 * Scheduler sched(100, job_data, BackfillPolicy::EASY,
 *                 PriorityPolicy::FCFS, RuntimeEstimateMode::USE_ACTUAL);
 *
 * // Submit jobs
 * auto jobs_to_start = sched.submit_jobs({0, 1, 2}, current_time);
 *
 * // Start jobs that got resources
 * auto started = sched.start_jobs(jobs_to_start, current_time);
 *
 * // Complete jobs
 * auto newly_ready = sched.end_jobs(started, current_time + duration);
 * @endcode
 *
 * @see BackfillPolicy, PriorityPolicy, RuntimeEstimateMode
 * @see ScheduleWindows for window tracking implementation
 */
class Scheduler {
  protected:
    num_nodes_t m_total_nodes;  ///< Total nodes in the system
    num_nodes_t m_free_nodes;   ///< Currently free nodes

    /// Jobs waiting to be scheduled (sorted by policy)
    std::set<job_no_t> m_wait_queue;

    /// Jobs with reservations: job_idx -> reservation_time
    std::map<job_no_t, sim_time_t> m_scheduled_jobs;

    /// Currently running jobs: job_idx -> actual_start_time
    std::map<job_no_t, sim_time_t> m_running_jobs;

    /// Scheduling policies
    BackfillPolicy m_backfill_policy;
    PriorityPolicy m_priority_policy;
    RuntimeEstimateMode m_runtime_mode;

    /// Schedule windows for backfill opportunity tracking
    ScheduleWindows m_schedule;

    /// Reference to job data
    const Trace::trace_data_t* m_job_data_ptr;

  public:
    /**
     * Constructor
     * @param total_nodes Total number of nodes in the system
     * @param job_data Reference to job trace data
     * @param bf_policy Backfill policy (default: EASY)
     * @param pri_policy Priority policy (default: FCFS)
     * @param rt_mode Runtime estimate mode (default: USE_LIMIT)
     */
    Scheduler(num_nodes_t total_nodes,
              const Trace::trace_data_t& job_data,
              BackfillPolicy bf_policy = BackfillPolicy::EASY,
              PriorityPolicy pri_policy = PriorityPolicy::FCFS,
              RuntimeEstimateMode rt_mode = RuntimeEstimateMode::USE_LIMIT);

    /**
     * Submit jobs to the scheduler
     * Adds jobs to wait queue and triggers scheduling
     * @param jobs List of job indices to submit
     * @param current_time Current simulation time
     * @return List of job indices that can start immediately
     */
    std::vector<job_no_t> submit_jobs(const std::vector<job_no_t>& jobs,
                                       sim_time_t current_time);

    /**
     * Start running jobs
     * Moves jobs from scheduled to running, allocates resources
     * @param jobs List of job indices to start
     * @param current_time Current simulation time
     * @return List of job indices that were successfully started
     */
    std::vector<job_no_t> start_jobs(const std::vector<job_no_t>& jobs,
                                      sim_time_t current_time);

    /**
     * End running jobs
     * Releases resources and triggers rescheduling
     * @param jobs List of job indices that have completed
     * @param current_time Current simulation time
     * @return List of job indices that can now start due to freed resources
     */
    std::vector<job_no_t> end_jobs(const std::vector<job_no_t>& jobs,
                                     sim_time_t current_time);

    /**
     * Get the reservation time for a job
     * @param job_idx Job index
     * @return Reservation time, or -1 if not scheduled
     */
    sim_time_t get_reservation_time(job_no_t job_idx) const;

    /**
     * Check if a job is in the wait queue
     */
    bool is_waiting(job_no_t job_idx) const;

    /**
     * Check if a job is scheduled (has a reservation)
     */
    bool is_scheduled(job_no_t job_idx) const;

    /**
     * Check if a job is currently running
     */
    bool is_running(job_no_t job_idx) const;

    /**
     * Get number of free nodes
     */
    num_nodes_t get_free_nodes() const { return m_free_nodes; }

    /**
     * Get number of jobs in wait queue
     */
    size_t num_waiting() const { return m_wait_queue.size(); }

    /**
     * Get number of scheduled jobs
     */
    size_t num_scheduled() const { return m_scheduled_jobs.size(); }

    /**
     * Get number of running jobs
     */
    size_t num_running() const { return m_running_jobs.size(); }

  protected:
    /**
     * Main scheduling routine - computes reservations and backfill opportunities
     * @param current_time Current simulation time
     * @return List of jobs that can start immediately
     */
    std::vector<job_no_t> trigger_schedule(sim_time_t current_time);

    /**
     * Find the earliest time a job can fit in the schedule
     * @param job_idx Job index
     * @param current_time Current simulation time
     * @return Earliest start time for the job
     */
    sim_time_t fit_in_schedule(job_no_t job_idx, sim_time_t current_time);

    /**
     * Sort jobs according to the priority policy
     * @param jobs Set of job indices
     * @return Sorted vector of job indices
     */
    std::vector<job_no_t> sort_jobs(const std::set<job_no_t>& jobs) const;

    /**
     * Get the runtime estimate for a job based on runtime mode
     * @param job_idx Job index
     * @return Estimated runtime
     */
    tdiff_t get_runtime_estimate(job_no_t job_idx) const;

    /**
     * Update the schedule after jobs end
     * Recalculates reservations that may have shifted earlier
     * @param current_time Current simulation time
     * @return List of jobs that can now start
     */
    std::vector<job_no_t> update_schedule(sim_time_t current_time);
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_SCHEDULER_HPP
