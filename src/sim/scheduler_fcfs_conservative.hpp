/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_CONSERVATIVE_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_CONSERVATIVE_HPP

#include "sim/scheduler_base.hpp"
#include <deque>
#include <vector>

namespace dr_evt {

/**
 * FCFS scheduler with Conservative backfilling support.
 *
 * Supports three backfill modes via BackfillPolicy:
 * - EASY: Only head job gets reservation (standard EASY backfilling)
 * - CONSERVATIVE: All queued jobs get reservations; backfill cannot delay ANY
 * job
 * - NONE: No backfilling (strict FCFS order)
 *
 * Based on FCFSScheduler with the same deque-based iterator tracking
 * optimization.
 *
 * NOTE: Currently only std::deque implementation exists. For optimal
 * performance, a CircularBufferFCFSConservativeScheduler should be implemented
 * (similar to CircularBufferFCFSScheduler) as circular buffer provides ~10-15%
 * better performance than deque for FCFS scheduling.
 */
class FCFSConservativeScheduler : public SchedulerBase {
private:
  struct JobEntry {
    /// Stable identifier of the Trace job represented by this entry.
    job_no_t job_id;
    /// Arrival time used to determine eligibility and FCFS order.
    sim_time_t submit_time;
    /// Requested time limit used to preserve reservations.
    tdiff_t run_time_estimate;
    /// Nodes requested when this job is started.
    num_nodes_t nodes_requested;
    /// True once selected for execution but retained for lazy deletion.
    bool removed;

    /** @brief Initialize an unscheduled conservative-FCFS queue entry.
     * @param[in] id Trace job identifier.
     * @param[in] submit Arrival time.
     * @param[in] run_time Requested runtime used for planning.
     * @param[in] nodes Requested node count. */
    JobEntry(job_no_t id, sim_time_t submit, tdiff_t run_time,
             num_nodes_t nodes)
        : job_id(id), submit_time(submit), run_time_estimate(run_time),
          nodes_requested(nodes), removed(false) {}
  };

  /// Submit-time-ordered jobs, including lazily removed entries.
  std::deque<JobEntry> m_wait_queue;
  /// First queue index with submit_time later than m_current_tracked_time.
  size_t m_eligible_end_idx;
  /// Latest time to which arrival eligibility has been synchronized.
  sim_time_t m_current_tracked_time;
  /// Removed entries inside the eligible prefix of m_wait_queue.
  size_t m_removed_count;

public:
  /** @brief Construct a conservative-backfill FCFS scheduler.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] job_data Trace owning every identifier later enqueued.
   * @param[in] bf_policy Backfill setting; conservative behavior is implemented
   * by this class.
   * @details @p job_data is retained by non-owning pointer and must outlive
   * this scheduler. */
  FCFSConservativeScheduler(num_nodes_t total_nodes, const Trace &job_data,
                            BackfillPolicy bf_policy)
      : SchedulerBase(total_nodes, job_data, bf_policy), m_eligible_end_idx(0),
        m_current_tracked_time(0.0), m_removed_count(0) {}

  /** @copydoc SchedulerBase::insert_job */
  void insert_job(job_no_t job_id, sim_time_t submit_time,
                  tdiff_t run_time_estimate,
                  num_nodes_t nodes_requested) override {
    m_wait_queue.emplace_back(job_id, submit_time, run_time_estimate,
                              nodes_requested);

    if (submit_time <= m_current_tracked_time) {
      m_eligible_end_idx = m_wait_queue.size();
    }
  }

  /** @copydoc SchedulerBase::schedule */
  std::vector<job_no_t>
  schedule(num_nodes_t free_nodes,
           const std::map<job_no_t, sim_time_t> &running_jobs,
           sim_time_t current_time) override;

  /** @copydoc SchedulerBase::sync_to */
  void sync_to(sim_time_t current_time) override;

  /** @copydoc SchedulerBase::active_job_count */
  size_t active_job_count() override {
    return m_eligible_end_idx - m_removed_count;
  }

  /** @copydoc SchedulerBase::get_next_arrival_time */
  sim_time_t get_next_arrival_time() override {
    for (size_t i = m_eligible_end_idx; i < m_wait_queue.size(); ++i) {
      if (!m_wait_queue[i].removed) {
        return m_wait_queue[i].submit_time;
      }
    }
    return std::numeric_limits<sim_time_t>::max();
  }

  /** @copydoc SchedulerBase::has_eligible_jobs */
  bool has_eligible_jobs() override { return active_job_count() > 0; }

protected:
  /** @copydoc SchedulerBase::wait_queue_size */
  size_t wait_queue_size() const override { return m_wait_queue.size(); }

private:
  /** @brief Lazily mark a scheduled queue entry as removed.
   * @param[in] job_id Trace job identifier to mark. */
  void mark_removed(job_no_t job_id);

  /**
   * @brief Calculate the reservation that a candidate must preserve.
   * @param[in] job_index Candidate position in FCFS queue order.
   * @param[in] available_nodes Nodes free at current_time.
   * @param[in] running_jobs Active jobs and their start times.
   * @param[in] current_time Projection time.
   * @return Earliest preserved reservation as sim_time_t.
   */
  sim_time_t calculate_conservative_window(
      size_t job_index, num_nodes_t available_nodes,
      const std::map<job_no_t, sim_time_t> &running_jobs,
      sim_time_t current_time);
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_CONSERVATIVE_HPP
