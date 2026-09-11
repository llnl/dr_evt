/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_SJF_HPP
#define DR_EVT_SIM_SCHEDULER_SJF_HPP

#include "sim/scheduler_base.hpp"
#include <map>
#include <set>

namespace dr_evt {

/**
 * Shortest Job First (SJF) scheduler with iterator tracking
 *
 * Uses std::multimap ordered by run_time for priority,
 * plus tracking of eligibility boundary to avoid rescanning.
 */
class SJFScheduler : public SchedulerBase {
public:
  /** @brief Construct a shortest-job-first scheduler.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] backfill_policy Rule governing lower-priority candidates. */
  SJFScheduler(num_nodes_t total_nodes, BackfillPolicy backfill_policy);

  /** @brief Add an existing trace job to the duration-ordered wait queue.
   * @param[in] job_id Stable Trace identifier.
   * @param[in] submit_time Arrival time controlling eligibility.
   * @param[in] run_time Requested limit used as the SJF key.
   * @param[in] nodes Requested node count used for fit checks. */
  void insert_job(job_no_t job_id, sim_time_t submit_time, tdiff_t run_time,
                  num_nodes_t nodes) override;

  /** @copydoc SchedulerBase::schedule */
  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                 const running_jobs_t &running_jobs,
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
    /// Requested time limit; also the SJF priority key.
    tdiff_t run_time;
    /// Nodes requested when this job is started.
    num_nodes_t nodes;
    // No removed flag: schedule() erases scheduled entries from
    // m_wait_queue immediately via the iterator it already holds
    // (multimap erase-by-iterator is O(1), so there's no benefit to
    // lazy deletion the way there is for a sequential container).
    // m_eligible_jobs is the single source of truth for "still
    // waiting" - every read site that used to check .removed was
    // either already also checking m_eligible_jobs membership
    // (redundant), or checking it in a context where a removed
    // entry could never appear anyway (see the accompanying patch
    // notes for the specific reasoning at each site).
  };

  /// Jobs ordered by shortest estimate, then stable Trace job identifier.
  std::multimap<tdiff_t, JobEntry> m_wait_queue;

  /// Identifiers of arrived jobs still waiting to be scheduled.
  std::set<job_no_t> m_eligible_jobs;

  /// Latest time to which arrival eligibility has been synchronized.
  sim_time_t m_current_tracked_time;

  /** @brief Add newly arrived jobs to the eligibility set. @param[in]
   * current_time New scheduler time. */
  void update_eligible_jobs(sim_time_t current_time);

  /** @brief Find the earliest-arriving eligible job. @return Iterator to the
   * FCFS head. */
  std::multimap<tdiff_t, JobEntry>::iterator find_fcfs_head();
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_SJF_HPP
