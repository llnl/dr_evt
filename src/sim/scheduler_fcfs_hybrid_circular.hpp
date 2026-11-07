/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_hybrid_circular.hpp
 * @brief Event-driven FCFS dispatch and hybrid/conservative backfill using a
 * circular queue; also the base implementation for periodic variants.
 */

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_HYBRID_CIRCULAR_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_HYBRID_CIRCULAR_HPP

#include "sim/scheduler_base.hpp"
#include <boost/circular_buffer.hpp>
#include <limits>
#include <stdexcept>

namespace dr_evt {

/**
 * @brief Event-driven FCFS dispatch and hybrid/conservative backfill using a
 * circular queue.
 * @details Selected directly for event-driven runs; periodic subclasses reuse
 * its queue and reservation logic with different triggers. This preserves
 * FCFSHybridDequeScheduler's reservation and lazy
 * removal rules while making its repeated indexed scans contiguous in memory.
 * The queue is fixed-capacity unless its configured overflow policy permits
 * growth; the deque-backed scheduler remains available through
 * QueueImplementation::DEQUE.
 * @note With --dispatch_interval 0 and --backfill_interval 0, selected by
 * --backfill_policy hybrid --reservation_depth N (N > 0) with FCFS ordering,
 * or --priority_policy fcfs_conservative with conservative/none backfill.
 * --queue_impl circular selects this queue; unsupported hybrid queue kinds
 * also fall back to it. --queue_impl deque selects FCFSHybridDequeScheduler.
 */
class FCFSHybridCircularScheduler : public SchedulerBase {
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

public:
  /**
   * @brief Construct a circular-buffer hybrid FCFS scheduler.
   * @details EASY has its own FCFS implementations; this class serves the
   * conservative and hybrid reservation policies (and no backfilling).
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] initial_job_count Initially known jobs, used when capacity is 0.
   * @param[in] bf_policy Conservative or hybrid reservation policy.
   * @throws std::invalid_argument if bf_policy is EASY.
   * @param[in] initial_capacity Initial queue capacity; zero uses job count.
   * @param[in] overflow_policy Action when the queue is full.
   * @param[in] reservation_depth Reservations retained by hybrid backfill.
   * @param[in] backfill_queue_depth Eligible waiting jobs examined per
   * scheduling timestamp; zero is unlimited.
   */
  FCFSHybridCircularScheduler(
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
        m_backfill_depth_examined(0), m_backfill_depth_head_accounted(false) {
    if (bf_policy == BackfillPolicy::EASY) {
      throw std::invalid_argument(
          "the hybrid circular scheduler does not implement EASY");
    }
  }

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
  /**
   * @brief Run the shared traversal with a compile-time protection planner.
   * @details Inlined to preserve the original vector-plan virtual entry point
   * for event-driven and separate periodic-backfill scheduling.
   * @tparam ProtectionTimeline Reservation_Timeline-compatible planner.
   * @param[in] free_nodes Nodes available for immediate starts.
   * @param[in] running_jobs Active jobs used for release projection.
   * @param[in] current_time Scheduling timestamp.
   * @return Job identifiers selected for immediate starts in queue order.
   */
  template <typename ProtectionTimeline>
  [[gnu::always_inline]] inline std::vector<job_no_t>
  schedule_with_protection(num_nodes_t free_nodes,
                           const running_jobs_t &running_jobs,
                           sim_time_t current_time);

  size_t wait_queue_size() const override { return m_wait_queue.size(); }

  /**
   * @brief Whether a job beyond the planning horizon still uses a depth slot.
   * @details A job whose own reservation would start beyond the planning
   * horizon holds no reservation and no reservation-depth slot in either mode.
   * Whether it also consumes a backfill queue-depth slot depends on the model
   * selected. The default event-driven traversal exempts horizon skips.
   * The periodic subclass counts candidates in its bounded prefix even
   * when their projected starts fall outside the planning horizon.
   * @return true when such a job, and a blocked head beyond the horizon, use
   * a queue-depth slot.
   */
  virtual bool horizon_skipped_job_uses_depth_slot() const { return false; }

private:
  /**
   * @brief Calculate the earliest start in the chained protected plan.
   * @param[in] job_index Candidate position in FCFS queue order.
   * @param[in] available_nodes Nodes free at current_time.
   * @param[in] running_jobs Active jobs used for release projection.
   * @param[in] current_time Projection time.
   * @param[in] projected_starts Jobs selected in this pass but not yet
   * published to running_jobs.
   * @return Earliest start in the chained protected plan.
   */
  sim_time_t
  calculate_hybrid_window(size_t job_index, num_nodes_t available_nodes,
                          const running_jobs_t &running_jobs,
                          sim_time_t current_time,
                          const projected_starts_t &projected_starts);
};

#include "sim/scheduler_fcfs_hybrid_circular.tpp"

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_HYBRID_CIRCULAR_HPP
