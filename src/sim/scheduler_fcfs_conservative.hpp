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

/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * FCFS scheduler with Conservative backfilling support.
 *
 * Supports four backfill modes via BackfillPolicy:
 * - EASY: Only head job gets reservation (standard EASY backfilling)
 * - CONSERVATIVE: All waiting jobs get reservations; backfill cannot delay
 *   any of them
 * - HYBRID: The first reservation_depth waiting jobs get reservations and
 *   backfill cannot delay them; deeper jobs hold no reservation. Depth 1
 *   reproduces EASY. This is Slurm's bf_job_part_count_reserve behaviour and
 *   Fluxion's hybrid queue policy.
 * - NONE: No backfilling (strict FCFS order)
 *
 * This is the deque-backed implementation. The default conservative scheduler
 * uses CircularBufferFCFSConservativeScheduler; select
 * QueueImplementation::DEQUE to retain this unbounded queue.
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
  /// Waiting jobs that hold reservations under HYBRID backfilling.
  size_t m_reservation_depth;
  size_t m_backfill_queue_depth;
  /// Timestamp for the shared backfill queue-depth accounting cycle.
  sim_time_t m_backfill_depth_time;
  /// Eligible in-horizon jobs examined in the current accounting cycle.
  size_t m_backfill_depth_examined;
  /// Whether the blocked FCFS head has consumed its slot in that cycle.
  bool m_backfill_depth_head_accounted;

public:
  /** @brief Construct a conservative-backfill FCFS scheduler.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] bf_policy Backfill setting; conservative and hybrid behavior
   * are implemented by this class.
   * @param[in] reservation_depth Number of waiting jobs, in queue order, that
   * hold reservations under HYBRID backfilling; ignored otherwise.
   * @param[in] backfill_queue_depth Eligible waiting jobs examined per
   * scheduling timestamp; zero is unlimited. */
  FCFSConservativeScheduler(num_nodes_t total_nodes, BackfillPolicy bf_policy,
                            size_t reservation_depth = 0,
                            size_t backfill_queue_depth = 0)
      : SchedulerBase(total_nodes, bf_policy), m_eligible_end_idx(0),
        m_current_tracked_time(0.0), m_removed_count(0),
        m_reservation_depth(reservation_depth),
        m_backfill_queue_depth(backfill_queue_depth),
        m_backfill_depth_time(std::numeric_limits<sim_time_t>::lowest()),
        m_backfill_depth_examined(0), m_backfill_depth_head_accounted(false) {}

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
  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                 const running_jobs_t &running_jobs,
                                 sim_time_t current_time) override;

  /** @copydoc SchedulerBase::sync_to */
  void sync_to(sim_time_t current_time) override;

  /** @copydoc SchedulerBase::active_job_count */
  size_t active_job_count() const override {
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

  /** @copydoc SchedulerBase::pending_job_ids */
  std::vector<job_no_t> pending_job_ids() const override {
    std::vector<job_no_t> result;
    for (const auto &job : m_wait_queue) {
      if (!job.removed) {
        result.push_back(job.job_id);
      }
    }
    return result;
  }

protected:
  /** @copydoc SchedulerBase::wait_queue_size */
  size_t wait_queue_size() const override { return m_wait_queue.size(); }

private:
  /** @brief Lazily mark a scheduled queue entry as removed.
   * @param[in] job_id Trace job identifier to mark. */
  void mark_removed(job_no_t job_id);

  /**
   * @brief Calculate the earliest start in the protected reservation plan.
   * @details Places protected waiting jobs ahead of the candidate into a
   * temporary resource-time plan in queue order. Under HYBRID only the first
   * m_reservation_depth waiting jobs are planned; a candidate deeper than
   * that must not delay any interval in that plan.
   * @param[in] job_index Candidate position in FCFS queue order.
   * @param[in] available_nodes Nodes free at current_time.
   * @param[in] running_jobs Active jobs with start time, runtime, and nodes.
   * @param[in] current_time Projection time.
   * @return Earliest start in the chained protected plan as sim_time_t.
   */
  sim_time_t calculate_conservative_window(size_t job_index,
                                           num_nodes_t available_nodes,
                                           const running_jobs_t &running_jobs,
                                           sim_time_t current_time);
};

/**@}*/

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_CONSERVATIVE_HPP
