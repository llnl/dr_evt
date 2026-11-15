/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_hybrid.hpp
 * @brief FCFS starts and hybrid/conservative backfill over a circular or
 * deque queue.
 * @details Hybrid protects up to R waiting jobs; conservative protects every
 * examined waiting job. Later jobs may start only when they preserve those
 * capacity reservations. Default examination accounting is shared across
 * calls at the same timestamp.
 * @note CLI: --priority_policy fcfs --backfill_policy hybrid
 * --reservation_depth R --queue_impl circular or deque, with R > 0. For
 * conservative use --priority_policy fcfs_conservative --backfill_policy
 * conservative.
 * Use --dispatch_interval 0 and --backfill_interval 0; omit
 * --per_pass_accounting.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_HYBRID_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_HYBRID_HPP

#include "sim/fcfs_wait_queue.hpp"
#include "sim/scheduler_base.hpp"
#include <limits>
#include <stdexcept>

namespace dr_evt {

/**
 * @brief Event-driven FCFS dispatch and hybrid/conservative backfill over a
 * chosen queue.
 * @tparam Queue Wait-queue storage policy: CircularWaitQueue or
 * DequeWaitQueue. The two differ only in how the waiting jobs are stored;
 * the scheduling logic is shared.
 * @details Selected directly for event-driven runs; periodic subclasses reuse
 * its queue and reservation logic with different triggers. Conservative is
 * the unlimited-reservation-depth case within the examined prefix.
 *
 * Supports three backfill modes via BackfillPolicy. EASY is not one of them;
 * it has its own FCFS schedulers, and constructing this class with EASY
 * throws.
 * - CONSERVATIVE: All waiting jobs get reservations; backfill cannot delay
 *   any of them.
 * - HYBRID: The first reservation_depth waiting jobs get reservations and
 *   backfill cannot delay them; deeper jobs hold no reservation. Depth 1
 *   reproduces EASY protection.
 * - NONE: No backfilling (strict FCFS order).
 *
 * A circular queue is contiguous in memory, which keeps the repeated indexed
 * scans cheap; it has a fixed capacity unless its overflow policy permits
 * growth. A deque grows without bound and ignores the capacity settings.
 * @note With --dispatch_interval 0 and --backfill_interval 0, selected by
 * --backfill_policy hybrid --reservation_depth N (N > 0) with FCFS ordering,
 * or --priority_policy fcfs_conservative with conservative/none backfill.
 * --queue_impl circular (default) or deque selects the queue; unsupported
 * hybrid queue kinds fall back to the circular one.
 */
template <typename Queue> class FCFSHybridScheduler : public SchedulerBase {
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

  /// Submit-time-ordered jobs, including lazily removed entries.
  typename Queue::template Storage<JobEntry> m_wait_queue;
  /// Action to take when a circular m_wait_queue reaches capacity.
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
   * @brief Construct a hybrid FCFS scheduler over the chosen queue.
   * @details EASY has its own FCFS implementations; this class serves the
   * conservative and hybrid reservation policies (and no backfilling).
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] initial_job_count Initially known jobs, used when capacity is
   * 0. Ignored by a deque.
   * @param[in] bf_policy Conservative or hybrid reservation policy.
   * @throws std::invalid_argument if bf_policy is EASY.
   * @param[in] initial_capacity Initial queue capacity; zero uses job count.
   * Ignored by a deque.
   * @param[in] overflow_policy Action when a circular queue is full. Ignored
   * by a deque.
   * @param[in] reservation_depth Reservations retained by hybrid backfill.
   * @param[in] backfill_queue_depth Eligible waiting jobs examined per
   * scheduling timestamp; zero is unlimited.
   */
  FCFSHybridScheduler(
      num_nodes_t total_nodes, size_t initial_job_count,
      BackfillPolicy bf_policy, size_t initial_capacity = 0,
      CircularOverflowPolicy overflow_policy = CircularOverflowPolicy::GROW,
      size_t reservation_depth = 0, size_t backfill_queue_depth = 0)
      : SchedulerBase(total_nodes, bf_policy),
        m_wait_queue(Queue::template create<JobEntry>(
            initial_capacity != 0 ? initial_capacity : initial_job_count)),
        m_overflow_policy(overflow_policy), m_eligible_end_idx(0),
        m_current_tracked_time(0.0), m_removed_count(0),
        m_reservation_depth(reservation_depth),
        m_backfill_queue_depth(backfill_queue_depth),
        m_backfill_depth_time(std::numeric_limits<sim_time_t>::lowest()),
        m_backfill_depth_examined(0), m_backfill_depth_head_accounted(false) {
    if (bf_policy == BackfillPolicy::EASY) {
      throw std::invalid_argument(
          "the hybrid scheduler does not implement EASY");
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

/** @brief Hybrid/conservative FCFS scheduler over a boost::circular_buffer. */
using FCFSHybridCircularScheduler = FCFSHybridScheduler<CircularWaitQueue>;
/** @brief Hybrid/conservative FCFS scheduler over a std::deque. */
using FCFSHybridDequeScheduler = FCFSHybridScheduler<DequeWaitQueue>;

// Defined in scheduler_fcfs_hybrid.cpp for both queues.
extern template class FCFSHybridScheduler<CircularWaitQueue>;
extern template class FCFSHybridScheduler<DequeWaitQueue>;

#include "sim/scheduler_fcfs_hybrid.tpp"

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_HYBRID_HPP
