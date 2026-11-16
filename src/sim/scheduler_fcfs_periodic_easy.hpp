/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_periodic_easy.hpp
 * @brief FCFS starts after events and EASY backfill on separate timer ticks.
 * @details Arrivals and resource releases can start fitting queue-head jobs
 * immediately. Later jobs are considered for backfill only on configured
 * ticks, while preserving the waiting head reservation. This is separate
 * periodic backfill, not periodic dispatch of the entire queue.
 * @note CLI: --priority_policy fcfs --queue_impl circular or deque
 * --backfill_policy easy --backfill_interval I, with I > 0 and
 * --dispatch_interval 0. Omit
 * --per_pass_accounting. --backfill_queue_depth limits backfill
 * examinations.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_PERIODIC_EASY_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_PERIODIC_EASY_HPP

#include "sim/scheduler_fcfs_easy.hpp"

#include <limits>

namespace dr_evt {

/**
 * @brief Event-driven FCFS dispatch and periodic EASY backfill over a chosen
 * queue.
 * @tparam Queue Wait-queue storage policy: CircularWaitQueue or
 * DequeWaitQueue.
 * @details Normal simulation events retain immediate FCFS dispatch through
 * schedule_event(); schedule() performs the usual EASY backfill scan when the
 * simulation reaches a configured tick.
 * @note Selected by --backfill_interval I (I > 0), --dispatch_interval 0,
 * --queue_impl circular or deque, --backfill_policy easy, and FCFS ordering.
 */
template <typename Queue>
class FCFSPeriodicEasyScheduler final : public FCFSEasyScheduler<Queue> {
  using Base = FCFSEasyScheduler<Queue>;
  using typename Base::JobEntry;

  sim_time_t m_backfill_evaluation_time =
      std::numeric_limits<sim_time_t>::lowest();

public:
  /**
   * @brief Construct a periodic EASY scheduler over the chosen queue.
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] initial_job_count Initially known jobs, used when capacity is
   * zero.
   * @param[in] initial_capacity Initial queue capacity; zero uses job count.
   * Ignored by a deque.
   * @param[in] overflow_policy Action when a circular queue is full. Ignored
   * by a deque.
   * @param[in] backfill_queue_depth Eligible jobs examined per periodic
   * backfill cycle; zero is unlimited.
   */
  FCFSPeriodicEasyScheduler(num_nodes_t total_nodes, size_t initial_job_count,
                            size_t initial_capacity,
                            CircularOverflowPolicy overflow_policy,
                            size_t backfill_queue_depth)
      : Base(total_nodes, initial_job_count, BackfillPolicy::EASY,
             initial_capacity, overflow_policy, backfill_queue_depth) {}

  /**
   * @brief Run at most one EASY backfill scan at a periodic tick.
   * @details Repeated calls at the same tick use schedule_event() so the
   * queue-depth budget remains one backfill cycle while FCFS dispatch can
   * still consume newly feasible head jobs.
   * @param[in] free_nodes Nodes currently available for allocation.
   * @param[in] running_jobs Jobs already running at this instant.
   * @param[in] current_time Current simulation time.
   * @return Job IDs selected by the periodic scheduling pass.
   */
  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                 const running_jobs_t &running_jobs,
                                 sim_time_t current_time) override;

  /** @copydoc SchedulerBase::schedule_event */
  std::vector<job_no_t> schedule_event(num_nodes_t free_nodes,
                                       const running_jobs_t &running_jobs,
                                       sim_time_t current_time) override;
};

/** @brief Periodic EASY FCFS scheduler over a boost::circular_buffer queue. */
using FCFSPeriodicEasyCircularScheduler =
    FCFSPeriodicEasyScheduler<CircularWaitQueue>;
/** @brief Periodic EASY FCFS scheduler over a std::deque queue. */
using FCFSPeriodicEasyDequeScheduler =
    FCFSPeriodicEasyScheduler<DequeWaitQueue>;

// Defined in scheduler_fcfs_periodic_easy.cpp for both queues.
extern template class FCFSPeriodicEasyScheduler<CircularWaitQueue>;
extern template class FCFSPeriodicEasyScheduler<DequeWaitQueue>;

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_PERIODIC_EASY_HPP
