/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_easy.hpp
 * @brief FCFS starts and EASY backfill over a circular or deque waiting
 * queue.
 * @details Jobs are considered in arrival order. When the head must wait,
 * protect its predicted start and allow later jobs only when they do not
 * delay it. All resources are interchangeable capacity counts.
 * @note CLI: --priority_policy fcfs --backfill_policy easy --queue_impl
 * circular or deque. Use --dispatch_interval 0 and --backfill_interval 0; omit
 * --per_pass_accounting. --backfill_policy none disables backfill.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_EASY_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_EASY_HPP

#include "sim/fcfs_wait_queue.hpp"
#include "sim/scheduler_base.hpp"
#include <algorithm>
#include <stdexcept>

namespace dr_evt {

/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * @brief Event-driven FCFS dispatch and EASY backfill over a chosen queue.
 * @tparam Queue Wait-queue storage policy: CircularWaitQueue or
 * DequeWaitQueue. The two differ only in how the waiting jobs are stored;
 * the scheduling logic is shared.
 * @details Selected directly for event-driven runs; the periodic EASY
 * subclass reuses this queue and backfill logic with separate timer triggers.
 * Jobs keep their submit-time order. A lazy-mark-and-compact scheme removes
 * jobs started by backfill, the head is consumed with pop_front(), and the
 * backfill scan is indexed. A boost::circular_buffer keeps its elements in
 * one contiguous block, so that indexed access is a direct offset; a
 * std::deque looks up a chunk first and then an offset within it.
 *
 * A circular queue has a fixed capacity: initial_capacity sets it (0
 * defaults to the initially known job count, large enough when each
 * initial job is submitted once), and overflow_policy decides what happens
 * when it is exceeded. ABORT throws, ending the simulation; GROW doubles the
 * capacity and copies the existing entries. A deque grows without bound and
 * ignores both settings.
 * @note Selected by --priority_policy fcfs --queue_impl circular (default) or
 * deque with --backfill_policy easy, --dispatch_interval 0, and
 * --backfill_interval 0. --priority_policy fcfs_conservative with EASY also
 * selects this implementation. --backfill_policy none disables backfill.
 */
template <typename Queue> class FCFSEasyScheduler : public SchedulerBase {
protected:
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
  typename Queue::template Storage<JobEntry> m_wait_queue;
  /// Action to take when a circular m_wait_queue reaches capacity.
  CircularOverflowPolicy m_overflow_policy;
  /// First queue index with submit_time later than m_current_tracked_time.
  size_t m_eligible_end_idx;
  /// Latest time to which arrival eligibility has been synchronized.
  sim_time_t m_current_tracked_time;
  /// Removed entries inside the eligible prefix of m_wait_queue.
  size_t m_removed_count;
  /// Jobs examined per backfill pass, head included; zero is unlimited.
  size_t m_backfill_queue_depth;

public:
  /**
   * @brief Construct an EASY FCFS scheduler over the chosen queue.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] initial_job_count Number of initially known jobs, used when
   * initial_capacity is zero. Ignored by a deque.
   * @param[in] bf_policy Rule governing jobs considered behind the FCFS head.
   * @param[in] initial_capacity Initial wait-queue capacity; zero derives one
   * from initial_job_count. Ignored by a deque.
   * @param[in] overflow_policy Action to take when that capacity is exhausted.
   * Ignored by a deque.
   * @param[in] backfill_queue_depth Jobs examined per backfill pass, head
   * included; zero is unlimited.
   */
  FCFSEasyScheduler(
      num_nodes_t total_nodes, size_t initial_job_count,
      BackfillPolicy bf_policy, size_t initial_capacity = 0,
      CircularOverflowPolicy overflow_policy = CircularOverflowPolicy::GROW,
      size_t backfill_queue_depth = 0)
      : SchedulerBase(total_nodes, bf_policy),
        m_wait_queue(Queue::template create<JobEntry>(
            initial_capacity != 0 ? initial_capacity : initial_job_count)),
        m_overflow_policy(overflow_policy), m_eligible_end_idx(0),
        m_current_tracked_time(0.0), m_removed_count(0),
        m_backfill_queue_depth(backfill_queue_depth) {}

  /** @copydoc SchedulerBase::insert_job */
  void insert_job(job_no_t job_id, sim_time_t submit_time,
                  tdiff_t run_time_estimate,
                  num_nodes_t nodes_requested) override {
    Queue::push_back(
        m_wait_queue,
        JobEntry(job_id, submit_time, run_time_estimate, nodes_requested),
        m_overflow_policy,
        std::string("FCFSEasy") + Queue::label + "Scheduler");

    // If this job is already eligible, advance index. Can jump by more
    // than 1 in a single call: if this new job's submit_time is
    // already <= current time, the sorted-submit-time invariant means
    // every entry already in the queue becomes eligible too.
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
    // Next arrival is at m_eligible_end_idx or later.
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

  /** @copydoc SchedulerBase::waiting_resource_area */
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
  /** @copydoc SchedulerBase::wait_queue_size */
  size_t wait_queue_size() const override { return m_wait_queue.size(); }

private:
  /** @brief Lazily mark a scheduled queue entry as removed.
   * @param[in] job_id Trace job identifier to mark. */
  void mark_removed(job_no_t job_id);
};

/** @brief EASY FCFS scheduler over a boost::circular_buffer queue. */
using FCFSEasyCircularScheduler = FCFSEasyScheduler<CircularWaitQueue>;
/** @brief EASY FCFS scheduler over a std::deque queue. */
using FCFSEasyDequeScheduler = FCFSEasyScheduler<DequeWaitQueue>;

// Defined in scheduler_fcfs_easy.cpp for both queues.
extern template class FCFSEasyScheduler<CircularWaitQueue>;
extern template class FCFSEasyScheduler<DequeWaitQueue>;

/**@}*/

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_EASY_HPP
