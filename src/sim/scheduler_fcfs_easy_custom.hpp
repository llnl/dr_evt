/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_easy_custom.hpp
 * @brief FCFS starts with an API-selected EASY backfill candidate.
 * @details Protect the waiting FCFS head, report feasible later candidates
 * to a caller-provided selector, and start the selected candidate. Resources
 * remain interchangeable capacity counts.
 * @note CLI: No CLI or Protobuf field activates this scheduler by itself.
 * Use the C++/Python Simulation constructor with cost and selection
 * callbacks. --num_max_candidates bounds the candidates offered after that
 * API path is selected. Dispatch and backfill intervals must be zero.
 * Protobuf: Simulation_Params.num_max_candidates sets the candidate limit;
 * InitRequest exposes no custom-selector callbacks or candidate limit.
 */

#ifndef DR_EVT_SIM_SCHEDULER_FCFS_EASY_CUSTOM_HPP
#define DR_EVT_SIM_SCHEDULER_FCFS_EASY_CUSTOM_HPP

#include "sim/scheduler_base.hpp"
#include <boost/circular_buffer.hpp>
#include <functional>
#include <optional>
#include <utility>

namespace dr_evt {

template <typename TraceType> class BasicSimulation;

/** \addtogroup dr_evt_sim
 *  @{ */

using backfill_candidate_t = std::pair<job_no_t, job_cost_t>;
using backfill_candidates_t = std::vector<backfill_candidate_t>;
using job_cost_function_t =
    std::function<job_cost_t(job_no_t, sim_time_t, tdiff_t, num_nodes_t)>;
using backfill_selector_t =
    std::function<std::optional<job_no_t>(const backfill_candidates_t &)>;

/**
 * @brief Event-driven FCFS dispatch and externally selected EASY backfill
 * using a circular queue.
 * @details
 * FCFS-head handling, circular-buffer growth, and EASY feasibility checks
 * match FCFSEasyCircularScheduler. When a job is inserted, a caller-provided
 * cost function computes the m_cost stored in its wait-queue entry. When the
 * head is blocked, up to num_max_candidates feasible (job_id, cost) pairs are
 * reported to a caller-provided selector. Only the candidate returned by that
 * selector is backfilled during the call.
 * @note Activated through the C++/Python Simulation constructor accepting
 * candidate limits, a cost callback, and a selection callback, with both
 * dispatch and backfill intervals zero. --num_max_candidates controls the
 * candidate limit after this API path is selected; no CLI flag activates
 * the custom scheduler by itself.
 */
class FCFSEasyCustomScheduler : public SchedulerBase {
private:
  template <typename TraceType> friend class BasicSimulation;

protected:
  /** Queue entry exposed read-only to scheduler subclasses. */
  struct JobEntry {
    job_no_t job_id;
    sim_time_t submit_time;
    tdiff_t run_time_estimate;
    num_nodes_t nodes_requested;
    job_cost_t m_cost;
    bool removed;

    JobEntry(job_no_t id, sim_time_t submit, tdiff_t run_time,
             num_nodes_t nodes, job_cost_t cost)
        : job_id(id), submit_time(submit), run_time_estimate(run_time),
          nodes_requested(nodes), m_cost(cost), removed(false) {}
  };

private:
  boost::circular_buffer<JobEntry> m_wait_queue;
  CircularOverflowPolicy m_overflow_policy;
  size_t m_eligible_end_idx;
  sim_time_t m_current_tracked_time;
  size_t m_removed_count;
  size_t m_num_max_candidates;
  size_t m_newly_eligible_begin_idx;
  bool m_arrival_update_pending;
  bool m_reevaluate_all_candidates;
  bool m_candidate_preparation_pending;
  job_cost_function_t m_job_cost_function;
  backfill_selector_t m_backfill_selector;

protected:
  /// Allocated-node time accumulated through m_resource_area_time.
  tdiff_t m_resource_area;
  /// Last simulation-time boundary incorporated into m_resource_area.
  sim_time_t m_resource_area_time;
  /// Boundary from which the current resource-area interval is measured.
  sim_time_t m_resource_area_start;
  /// Free nodes after the most recently settled scheduling cycle.
  num_nodes_t m_accounted_available_nodes;
  /// Jobs examined per backfill pass, head included, before the feasible
  /// candidates are capped by m_num_max_candidates; zero is unlimited.
  size_t m_backfill_queue_depth = 0;

  /** Close the resource-accounting interval ending at current_time. */
  void advance_resource_accounting_to(sim_time_t current_time);

  /** Store free capacity after all scheduling at the current time settles. */
  void commit_available_nodes(num_nodes_t available_nodes);

  /** Reset resource accounting for a traditional empty time-zero start. */
  void reset_resource_accounting();

  /** Reset resource accounting at a populated simulation boundary. */
  void reset_resource_accounting(sim_time_t start_time,
                                 num_nodes_t available_nodes);

  /** Return allocated-node area through a finite snapshot time. */
  tdiff_t resource_area_through(sim_time_t through_time) const;

  /**
   * Return time-accounted utilization through a finite snapshot time.
   * This protected accessor is available to experimental subclasses.
   */
  double utilization_through(sim_time_t through_time) const;

  /** Compute queued node-time on demand from existing queue entries. */
  std::optional<tdiff_t> waiting_resource_area() const override;

  /**
   * Construct the FCFS scheduling core for a subclass-owned selection policy.
   * The subclass must override select_backfill_candidate() and may insert
   * precomputed queue costs with insert_job_with_cost().
   */
  FCFSEasyCustomScheduler(
      num_nodes_t total_nodes, size_t initial_job_count,
      BackfillPolicy bf_policy, size_t num_max_candidates,
      size_t initial_capacity,
      CircularOverflowPolicy overflow_policy = CircularOverflowPolicy::GROW);

  /** Insert a job with a cost computed by a subclass-owned policy. */
  void insert_job_with_cost(job_no_t job_id, sim_time_t submit_time,
                            tdiff_t run_time_estimate,
                            num_nodes_t nodes_requested, job_cost_t cost);

  /** Return a read-only view of all stored queue entries. */
  const boost::circular_buffer<JobEntry> &queued_jobs() const {
    return m_wait_queue;
  }

  /** Return the exclusive end of the currently eligible queue range. */
  size_t eligible_job_end() const { return m_eligible_end_idx; }

  /** Allow a subclass to choose from the bounded feasible candidate set. */
  virtual std::optional<job_no_t> select_backfill_candidate(
      const backfill_candidates_t &candidates, num_nodes_t available_nodes,
      const running_jobs_t &effective_running_jobs, sim_time_t current_time);

  /**
   * Called once after a complete same-timestamp arrival batch becomes
   * eligible and before any FCFS dispatch from that batch.
   */
  virtual void on_jobs_became_eligible(size_t newly_eligible_begin,
                                       size_t eligible_end,
                                       num_nodes_t available_nodes,
                                       const running_jobs_t &running_jobs,
                                       sim_time_t current_time);

  /**
   * Called once after FCFS dispatch and before the first backfill selection
   * for an event cycle.
   */
  virtual void
  on_backfill_candidates_ready(const backfill_candidates_t &candidates,
                               num_nodes_t available_nodes,
                               const running_jobs_t &effective_running_jobs,
                               sim_time_t current_time, bool fcfs_jobs_started);

  /** Called after no additional job can be dispatched at the current time. */
  virtual void on_scheduling_cycle_complete(num_nodes_t available_nodes,
                                            const running_jobs_t &running_jobs,
                                            sim_time_t current_time);

public:
  /**
   * @param[in] total_nodes Cluster capacity available for allocations.
   * @param[in] initial_job_count Number of initially known jobs, used when
   * initial_capacity is zero.
   * @param[in] bf_policy Backfill policy. External selection is used for EASY.
   * @param[in] num_max_candidates Maximum feasible jobs shown per decision.
   * @param[in] cost_function Function called at insertion to compute m_cost
   * from job ID, submit time, estimated runtime, and requested nodes.
   * @param[in] selector Function returning a candidate job ID, or nullopt to
   * make no backfill selection. The selected ID must occur in its input.
   * @param[in] initial_capacity Initial circular-buffer capacity; zero derives
   * one from initial_job_count.
   * @param[in] overflow_policy Action when the circular buffer is full.
   */
  FCFSEasyCustomScheduler(
      num_nodes_t total_nodes, size_t initial_job_count,
      BackfillPolicy bf_policy, size_t num_max_candidates,
      job_cost_function_t cost_function, backfill_selector_t selector,
      size_t initial_capacity = 0,
      CircularOverflowPolicy overflow_policy = CircularOverflowPolicy::GROW);

  /**
   * @brief Insert a job and compute its cost with m_job_cost_function.
   */
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

  /**
   * @brief Bound the jobs examined per backfill pass (--backfill_queue_depth).
   * @details Applied before feasibility testing, the same way as in the
   * built-in FCFS schedulers; num_max_candidates then caps the feasible jobs
   * offered to the selector. The two limits act at different stages.
   * @param[in] depth Jobs examined per pass, head included; zero is unlimited.
   */
  void set_backfill_queue_depth(size_t depth) {
    m_backfill_queue_depth = depth;
  }

  /**
   * @brief Identify feasible backfill jobs without changing queue state.
   * @details The queue must already be synchronized and have a blocked FCFS
   * head at index zero. Only the first jobs within the backfill queue depth
   * are examined. Candidates retain FCFS order and are capped by the
   * constructor's num_max_candidates value.
   */
  backfill_candidates_t
  find_backfill_candidates(num_nodes_t available_nodes, sim_time_t current_time,
                           sim_time_t reservation_time) const;

protected:
  size_t wait_queue_size() const override { return m_wait_queue.size(); }

private:
  /** Request a full candidate scan after completions or capacity changes. */
  void notify_resource_change() {
    m_reevaluate_all_candidates = true;
    m_candidate_preparation_pending = true;
  }

  /** Complete one event-time scheduling cycle and reset scan tracking. */
  void finish_scheduling_cycle(num_nodes_t available_nodes,
                               const running_jobs_t &running_jobs,
                               sim_time_t current_time);

  void complete_candidate_scan() {
    m_newly_eligible_begin_idx = m_eligible_end_idx;
    m_reevaluate_all_candidates = false;
    m_candidate_preparation_pending = false;
  }

  backfill_candidates_t find_backfill_candidates_from(
      size_t begin_idx, num_nodes_t available_nodes, sim_time_t current_time,
      sim_time_t reservation_time, const running_jobs_t &running_jobs,
      const projected_starts_t &projected_starts = {}) const;

  void compact_if_needed();
};

/**@}*/

} // namespace dr_evt

#endif // DR_EVT_SIM_SCHEDULER_FCFS_EASY_CUSTOM_HPP
