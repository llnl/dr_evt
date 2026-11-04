/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_circular_fcfs_conservative.hpp
 * @brief Circular-buffer wait queue for conservative FCFS backfilling.
 */

#ifndef DR_EVT_SIM_SCHEDULER_CIRCULAR_FCFS_CONSERVATIVE_HPP
#define DR_EVT_SIM_SCHEDULER_CIRCULAR_FCFS_CONSERVATIVE_HPP

#include "sim/scheduler_base.hpp"
#include <boost/circular_buffer.hpp>
#include <limits>
#include <stdexcept>

namespace dr_evt {

/**
 * @brief Conservative FCFS scheduler backed by a contiguous circular buffer.
 * @details This preserves FCFSConservativeScheduler's reservation and lazy
 * removal rules while making its repeated indexed scans contiguous in memory.
 * The queue is fixed-capacity unless its configured overflow policy permits
 * growth; the deque-backed scheduler remains available through
 * QueueImplementation::DEQUE.
 */
class CircularBufferFCFSConservativeScheduler : public SchedulerBase {
protected:
  struct JobEntry {
    job_no_t job_id;
    sim_time_t submit_time;
    tdiff_t run_time_estimate;
    num_nodes_t nodes_requested;
    bool removed;

    JobEntry(job_no_t id, sim_time_t submit, tdiff_t run_time,
             num_nodes_t nodes)
        : job_id(id), submit_time(submit), run_time_estimate(run_time),
          nodes_requested(nodes), removed(false) {}
  };

  boost::circular_buffer<JobEntry> m_wait_queue;
  CircularOverflowPolicy m_overflow_policy;
  size_t m_eligible_end_idx;
  sim_time_t m_current_tracked_time;
  size_t m_removed_count;
  size_t m_reservation_depth;
  size_t m_backfill_queue_depth;
  /// Timestamp for the shared backfill queue-depth accounting cycle.
  sim_time_t m_backfill_depth_time;
  /// Eligible in-horizon jobs examined in the current accounting cycle.
  size_t m_backfill_depth_examined;
  /// Whether the blocked FCFS head has consumed its slot in that cycle.
  bool m_backfill_depth_head_accounted;
  /// True when one backfill cycle spans every pass at a timestamp for the EASY
  /// scan as well. Conservative and hybrid scans always share their count;
  /// the periodic subclass sets this so that all policies treat one tick as
  /// one cycle, the way Slurm resets bf_max_job_test per backfill iteration.
  bool m_share_easy_depth_across_passes = false;

  /**
   * @brief End of the EASY candidate scan, sharing the depth across passes.
   * @details Starts a new accounting cycle when time has advanced, charges an
   * in-horizon blocked head once per cycle, then charges every eligible job
   * the scan will examine against the remaining budget. A later pass at the
   * same timestamp therefore continues from the count left by the earlier
   * pass instead of starting from zero.
   * @param[in] begin First candidate index (the entry after the head).
   * @param[in] end One past the last eligible index.
   * @param[in] head_planned Whether the head's reservation is inside the
   * planning horizon and therefore consumes a slot.
   * @param[in] current_time Timestamp of the pass.
   * @return Index at which the candidate scan stops, in [begin, end].
   */
  size_t shared_easy_scan_end(size_t begin, size_t end, bool head_planned,
                              sim_time_t current_time);

public:
  /**
   * @brief Construct a circular-buffer conservative FCFS scheduler.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] initial_job_count Initially known jobs, used when capacity is 0.
   * @param[in] bf_policy Conservative or hybrid reservation policy.
   * @param[in] initial_capacity Initial queue capacity; zero uses job count.
   * @param[in] overflow_policy Action when the queue is full.
   * @param[in] reservation_depth Reservations retained by hybrid backfill.
   * @param[in] backfill_queue_depth Eligible waiting jobs examined per
   * scheduling timestamp; zero is unlimited.
   */
  CircularBufferFCFSConservativeScheduler(
      num_nodes_t total_nodes, size_t initial_job_count,
      BackfillPolicy bf_policy, size_t initial_capacity = 0,
      CircularOverflowPolicy overflow_policy = CircularOverflowPolicy::GROW,
      size_t reservation_depth = 0, size_t backfill_queue_depth = 0)
      : SchedulerBase(total_nodes, bf_policy),
        m_wait_queue(initial_capacity != 0 ? initial_capacity
                                           : initial_job_count),
        m_overflow_policy(overflow_policy), m_eligible_end_idx(0),
        m_current_tracked_time(0.0), m_removed_count(0),
        m_reservation_depth(reservation_depth),
        m_backfill_queue_depth(backfill_queue_depth),
        m_backfill_depth_time(std::numeric_limits<sim_time_t>::lowest()),
        m_backfill_depth_examined(0), m_backfill_depth_head_accounted(false) {}

  void insert_job(job_no_t job_id, sim_time_t submit_time,
                  tdiff_t run_time_estimate,
                  num_nodes_t nodes_requested) override;
  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                 const running_jobs_t &running_jobs,
                                 sim_time_t current_time) override;
  void sync_to(sim_time_t current_time) override;
  size_t active_job_count() const override {
    return m_eligible_end_idx - m_removed_count;
  }
  sim_time_t get_next_arrival_time() override;
  bool has_eligible_jobs() override { return active_job_count() > 0; }
  std::vector<job_no_t> pending_job_ids() const override;

protected:
  size_t wait_queue_size() const override { return m_wait_queue.size(); }

private:
  /** Build the chained protected reservation plan before job_index and return
   * its earliest planned start. */
  sim_time_t calculate_conservative_window(size_t job_index,
                                           num_nodes_t available_nodes,
                                           const running_jobs_t &running_jobs,
                                           sim_time_t current_time);
};

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_CIRCULAR_FCFS_CONSERVATIVE_HPP
