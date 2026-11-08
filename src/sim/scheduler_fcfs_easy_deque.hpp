/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_easy_deque.hpp
 * @brief FCFS starts and EASY backfill using a deque waiting queue.
 * @details Jobs are considered in arrival order. When the head must wait,
 * protect its predicted start and allow later jobs only when they do not
 * delay it. All resources are interchangeable capacity counts.
 * @note CLI: --priority_policy fcfs --backfill_policy easy --queue_impl
 * deque. Use --dispatch_interval 0 and --backfill_interval 0; omit
 * --per_pass_accounting. --backfill_policy none disables backfill.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_EASY_DEQUE_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_EASY_DEQUE_HPP

#include "sim/scheduler_base.hpp"
#include <deque>

namespace dr_evt {

/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * @brief Event-driven FCFS dispatch and EASY backfill using a deque queue.
 * @details
 * Key optimization: Maintains iterator marking boundary between
 * "eligible" (submit_time <= current_time) and "not yet eligible" jobs.
 *
 * No rescanning or submit_time comparison in schedule().
 * @note Selected by --priority_policy fcfs --queue_impl deque
 * --backfill_policy easy, with --dispatch_interval 0 and --backfill_interval 0.
 * --priority_policy fcfs_conservative with EASY also selects this queue.
 * --backfill_policy none disables backfill.
 */
class FCFSEasyDequeScheduler : public SchedulerBase {
private:
  struct JobEntry {
    /// Stable identifier of the Trace job represented by this entry.
    job_no_t job_id;
    /// Arrival time used to determine eligibility and FCFS order.
    sim_time_t submit_time;
    /// Requested time limit used for reservation and backfill projection.
    tdiff_t run_time_estimate;
    /// Nodes requested when this job is started.
    num_nodes_t nodes_requested;
    /// True once selected for execution but retained for lazy deletion.
    bool removed;

    /** @brief Initialize an unscheduled FCFS queue entry.
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
  /// Jobs examined per backfill pass, head included; zero is unlimited.
  size_t m_backfill_queue_depth;

public:
  /** @brief Construct an FCFS scheduler.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] bf_policy Rule governing jobs considered behind the FCFS head.
   * @param[in] backfill_queue_depth Jobs examined per backfill pass, head
   * included; zero is unlimited.
   */
  FCFSEasyDequeScheduler(num_nodes_t total_nodes, BackfillPolicy bf_policy,
                         size_t backfill_queue_depth = 0)
      : SchedulerBase(total_nodes, bf_policy), m_eligible_end_idx(0),
        m_current_tracked_time(0.0), m_removed_count(0),
        m_backfill_queue_depth(backfill_queue_depth) {}

  /** @copydoc SchedulerBase::insert_job */
  void insert_job(job_no_t job_id, sim_time_t submit_time,
                  tdiff_t run_time_estimate,
                  num_nodes_t nodes_requested) override {
    m_wait_queue.emplace_back(job_id, submit_time, run_time_estimate,
                              nodes_requested);

    // If this job is already eligible, advance index. Can jump by more
    // than 1 in a single call: if this new job's submit_time is
    // already <= current time, the sorted-submit-time invariant means
    // every entry already in the deque becomes eligible too.
    if (submit_time <= m_current_tracked_time) {
      m_eligible_end_idx = m_wait_queue.size();
    }
  }

  /** @copydoc SchedulerBase::schedule */
  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                 const running_jobs_t &running_jobs,
                                 sim_time_t current_time) override;

  /**
   * Advance m_eligible_end_idx to reflect current_time. Idempotent and
   * cheap to call redundantly (see body: early-returns if current_time
   * hasn't advanced past what's already tracked). schedule() calls
   * this unconditionally at its own top, so it stays correct
   * regardless of whether an external caller already synced.
   * @param[in] current_time Simulation time through which arrivals become
   * eligible.
   */
  void sync_to(sim_time_t current_time) override;

  /**
   * Count WAITING jobs (arrived but not scheduled), as of whatever
   * time this scheduler was last synced to via sync_to(). Derived, not
   * scanned: every removed entry lives within [0, m_eligible_end_idx)
   * by construction (removal only ever happens to entries that are
   * already eligible - see schedule() and mark_removed()), so
   * "eligible minus removed" is exactly "waiting".
   *
   * Invariants:
   * - m_wait_queue is sorted by submit_time (jobs inserted from sorted trace)
   * - m_eligible_end_idx advances monotonically as time advances
   * - removed flag is set either by mark_removed(), or directly within
   *   schedule() at call sites where the index is already known (to
   *   avoid mark_removed()'s O(n) linear re-scan by job id)
   */
  size_t active_job_count() const override {
    return m_eligible_end_idx - m_removed_count;
  }

  /** @copydoc SchedulerBase::get_next_arrival_time */
  sim_time_t get_next_arrival_time() override {
    // Next arrival is at m_eligible_end_idx or later
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

  std::optional<tdiff_t> waiting_resource_area() const override {
    tdiff_t area = 0.0;
    for (size_t i = 0; i < m_eligible_end_idx; ++i) {
      const auto &job = m_wait_queue[i];
      if (!job.removed) {
        area +=
            static_cast<tdiff_t>(job.nodes_requested) * job.run_time_estimate;
      }
    }
    return area;
  }

protected:
  /**
   * Return total size of wait queue (ALL jobs, all states).
   * Includes future arrivals, waiting jobs, and scheduled (removed) jobs.
   * Internal utility only - use active_job_count() externally.
   */
  /** @copydoc SchedulerBase::wait_queue_size */
  size_t wait_queue_size() const override { return m_wait_queue.size(); }

private:
  /** @brief Mark a scheduled queue entry without invalidating deque iterators.
   * @param[in] job_id Trace job identifier to mark. */
  void mark_removed(job_no_t job_id);
};

/**@}*/

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_EASY_DEQUE_HPP
