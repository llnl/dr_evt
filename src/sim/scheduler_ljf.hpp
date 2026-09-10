/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_LJF_HPP
#define DR_EVT_SIM_SCHEDULER_LJF_HPP

#include "sim/scheduler_base.hpp"
#include <map>
#include <set>

namespace dr_evt {

/**
 * Longest Job First (LJF) scheduler with iterator tracking
 *
 * Uses std::map with descending order (longest run_time first),
 * plus tracking of eligibility boundary to avoid rescanning.
 */
class LJFScheduler : public SchedulerBase {
public:
  /** @brief Construct a longest-job-first scheduler over an existing trace.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] job_data Trace owning every identifier later enqueued.
   * @param[in] backfill_policy Rule governing lower-priority candidates.
   * @details @p job_data is retained by non-owning pointer and must outlive
   * this scheduler. */
  LJFScheduler(num_nodes_t total_nodes, const Trace &job_data,
               BackfillPolicy backfill_policy);

  /** @brief Add an existing trace job to the duration-ordered wait queue.
   * @param[in] job_id Stable Trace identifier.
   * @param[in] submit_time Arrival time controlling eligibility.
   * @param[in] run_time Requested limit used as the LJF key.
   * @param[in] nodes Requested node count used for fit checks. */
  void insert_job(job_no_t job_id, sim_time_t submit_time, tdiff_t run_time,
                  num_nodes_t nodes) override;

  /** @copydoc SchedulerBase::schedule */
  std::vector<job_no_t>
  schedule(num_nodes_t free_nodes,
           const std::map<job_no_t, sim_time_t> &running_jobs,
           sim_time_t current_time) override;

  /** @copydoc SchedulerBase::sync_to */
  void sync_to(sim_time_t current_time) override {
    update_eligible_jobs(current_time);
  }

  /** @copydoc SchedulerBase::active_job_count */
  size_t active_job_count() override { return m_eligible_jobs.size(); }

  /** @copydoc SchedulerBase::get_next_arrival_time */
  sim_time_t get_next_arrival_time() override {
    // m_current_tracked_time is exactly "whatever time this
    // scheduler was last synced to via sync_to()" - using it here
    // instead of a parameter is equivalent, given the caller has
    // already synced before calling this.
    sim_time_t next = std::numeric_limits<sim_time_t>::max();
    for (const auto &pair : m_wait_queue) {
      const JobEntry &entry = pair.second;
      if (entry.submit_time > m_current_tracked_time &&
          entry.submit_time < next) {
        next = entry.submit_time;
      }
    }
    return next;
  }

  /** @copydoc SchedulerBase::has_eligible_jobs */
  bool has_eligible_jobs() override { return !m_eligible_jobs.empty(); }

protected:
  /** @copydoc SchedulerBase::wait_queue_size */
  size_t wait_queue_size() const override { return m_wait_queue.size(); }

private:
  struct JobEntry {
    /// Stable identifier of the Trace job represented by this entry.
    job_no_t job_id;
    /// Arrival time used to determine eligibility.
    sim_time_t submit_time;
    /// Requested time limit; also the LJF priority key.
    tdiff_t run_time;
    /// Nodes requested when this job is started.
    num_nodes_t nodes;
    // No removed flag - see SJFScheduler for full reasoning.
    // schedule() erases scheduled entries from m_wait_queue
    // immediately via the iterator it already holds.
  };

  /// Orders estimates from longest to shortest for the multimap key.
  struct DescendingRunTime {
    /** @brief Order larger run-time estimates before smaller ones.
     * @param[in] a Left runtime estimate.
     * @param[in] b Right runtime estimate.
     * @return `true` when @p a should precede @p b. */
    bool operator()(tdiff_t a, tdiff_t b) const {
      return a > b; // Reverse: larger run_time comes first
    }
  };

  /// Jobs ordered by longest estimate, then stable Trace job identifier.
  std::multimap<tdiff_t, JobEntry, DescendingRunTime> m_wait_queue;

  /// Identifiers of arrived jobs still waiting to be scheduled.
  std::set<job_no_t> m_eligible_jobs;

  /// Latest time to which arrival eligibility has been synchronized.
  sim_time_t m_current_tracked_time;

  /** @brief Add newly arrived jobs to the eligibility set. @param[in]
   * current_time New scheduler time. */
  void update_eligible_jobs(sim_time_t current_time);

  /** @brief Find the earliest-arriving eligible job. @return Iterator to the
   * FCFS head. */
  std::multimap<tdiff_t, JobEntry, DescendingRunTime>::iterator
  find_fcfs_head();
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_LJF_HPP
