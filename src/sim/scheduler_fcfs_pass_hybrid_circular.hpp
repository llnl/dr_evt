/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_pass_hybrid_circular.hpp
 * @brief Event-driven FCFS queue scans with limits reset for each scan.
 * @details A pass is one attempt to scan the waiting queue at the current
 * simulation time. Starts and successful reservations count toward Q; failed
 * tests after reaching R also count. A new pass restarts at the head with
 * fresh Q/R, without advancing time. Identical reservation-only retries are
 * stopped by a simulation guard. The OpenPBS subclass reuses this scan with
 * its own forecast.
 * @note CLI: --per_pass_accounting --priority_policy fcfs --queue_impl
 * circular. --backfill_queue_depth sets Q (0 is unlimited).
 * --backfill_policy hybrid --reservation_depth R protects R jobs; easy
 * protects one and conservative protects all examined jobs. Keep both
 * intervals zero and omit --planning_horizon for unlimited look-ahead.
 * --priority_policy fcfs_1s_evt_group selects the OpenPBS forecast without
 * requiring --per_pass_accounting.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_PASS_HYBRID_CIRCULAR_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_PASS_HYBRID_CIRCULAR_HPP

#include "sim/scheduler_fcfs_hybrid_circular.hpp"
#include "sim/resource/count_model.hpp"
#include <unordered_set>

namespace dr_evt {

/**
 * @brief FCFS allocation/reservation passes using a circular waiting queue.
 * @details Immediate allocations and successful future reservations consume
 * Q. After R successful reservations, later jobs receive allocation-only tests;
 * failed allocation-only tests consume Q. Jobs with no allocation or future
 * reservation consume neither count and are blocked until a state event.
 * Each new pass starts at the head with fresh Q/R and a rebuilt reservation
 * plan. Reaching Q with remaining jobs and a successful action requests another
 * pass, including when the successful action was only a reservation.
 * @note Selected by --per_pass_accounting with FCFS ordering,
 * --queue_impl circular, and zero dispatch/backfill intervals. Q is supplied
 * by --backfill_queue_depth; R by --reservation_depth under hybrid, 1 under
 * EASY, and unlimited under conservative. This implementation always uses
 * resource-time protection, independently of DR_EVT_LEGACY_RESERVATION_BARRIER.
 * It models synchronous policy passes, not asynchronous RPC callback timing.
 * A tentative guard stops identical reservation-only passes so simulation
 * time can advance. Replace or confirm this guard after the native restart
 * handling is fully understood. Existing scheduler traversals are unused.
 */
class FCFSPassHybridCircularScheduler : public FCFSHybridCircularScheduler {
  /// Jobs with no feasible start; retain queue order but skip further tests.
  std::unordered_set<job_no_t> m_blocked;
  sim_time_t m_state_time = std::numeric_limits<sim_time_t>::lowest();
  num_nodes_t m_state_free = 0;
  size_t m_state_running = 0;
  num_nodes_t m_state_capacity = 0;
  const Capacity_Change *m_state_changes = nullptr;
  size_t m_last_pass_count = 0;
  bool m_last_retry_stopped = false;

protected:
  /**
   * @brief Run placement passes using a compile-time forecast planner.
   * @tparam Timeline Reservation_Timeline-compatible forecast model.
   * @param[in] free_nodes Capacity available for immediate starts.
   * @param[in] running_jobs Running jobs and requested release estimates.
   * @param[in] current_time Actual scheduling time, unchanged by forecasting.
   * @return Jobs selected for immediate execution.
   */
  template <typename Timeline, typename Model = ActiveCountModel>
  std::vector<job_no_t>
  schedule_with_forecast(num_nodes_t free_nodes,
                         const running_jobs_t &running_jobs,
                         sim_time_t current_time);

public:
  /**
   * @brief Construct a per-pass placement scheduler.
   * @param[in] total_nodes Cluster capacity.
   * @param[in] initial_job_count Initially known jobs.
   * @param[in] policy EASY, hybrid, conservative, or strict FCFS.
   * @param[in] initial_capacity Circular queue capacity; zero uses job count.
   * @param[in] overflow_policy Queue overflow behavior.
   * @param[in] reservation_depth Hybrid reservation count R.
   * @param[in] queue_depth Counted placement actions Q; zero is unlimited.
   */
  FCFSPassHybridCircularScheduler(
      num_nodes_t total_nodes, size_t initial_job_count, BackfillPolicy policy,
      size_t initial_capacity = 0,
      CircularOverflowPolicy overflow_policy = CircularOverflowPolicy::GROW,
      size_t reservation_depth = 0, size_t queue_depth = 0)
      : FCFSHybridCircularScheduler(
            total_nodes, initial_job_count,
            policy == BackfillPolicy::EASY ? BackfillPolicy::HYBRID : policy,
            initial_capacity, overflow_policy, reservation_depth, queue_depth) {
    m_backfill_policy = policy;
    if (policy == BackfillPolicy::HYBRID && reservation_depth == 0) {
      throw std::invalid_argument(
          "hybrid backfilling requires reservation_depth > 0");
    }
  }

  /**
   * @brief Insert a pending job and clear cached blocked-job decisions.
   * @param[in] id Existing Trace job identifier.
   * @param[in] submit Arrival time used for eligibility and ordering.
   * @param[in] duration Time-limit estimate used for reservations.
   * @param[in] nodes Requested node count.
   */
  void insert_job(job_no_t id, sim_time_t submit, tdiff_t duration,
                  num_nodes_t nodes) override {
    FCFSHybridCircularScheduler::insert_job(id, submit, duration, nodes);
    m_blocked.clear();
  }

  /** @copydoc SchedulerBase::schedule */
  std::vector<job_no_t> schedule(num_nodes_t free_nodes,
                                 const running_jobs_t &running_jobs,
                                 sim_time_t current_time) override;

  /** @brief Number of placement passes in the most recent schedule call. */
  size_t last_pass_count() const { return m_last_pass_count; }
  /** @brief Whether the most recent call stopped an unchanged retry plan. */
  bool last_retry_stopped() const { return m_last_retry_stopped; }
};

} // namespace dr_evt
#endif
