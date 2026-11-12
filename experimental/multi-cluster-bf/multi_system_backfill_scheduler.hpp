/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file multi_system_backfill_scheduler.hpp
 * @brief FCFS scheduling over several systems with spatial backfilling.
 * @details
 * One FCFS wait queue is shared by K systems of possibly unequal capacity.
 * The core Simulation sees a single pool whose size is the sum of the
 * system capacities; this scheduler enforces the partition internally by
 * remembering where every job it started was placed and deriving per-system
 * usage from the running-job map it is handed on each pass. See the README
 * in this directory for the policy definitions.
 */

#ifndef DR_EVT_EXPERIMENTAL_MULTI_SYSTEM_BACKFILL_SCHEDULER_HPP
#define DR_EVT_EXPERIMENTAL_MULTI_SYSTEM_BACKFILL_SCHEDULER_HPP

#include "sim/scheduler_fcfs_easy_custom.hpp"
#include <deque>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dr_evt {
namespace experimental {

/// Index of a system in the configured systems table.
using system_id_t = unsigned;

/// Window rule applied to backfill candidates on each system.
enum class SpatialBackfillPolicy {
  TARGET_ONLY,   ///< Protect only the head's best system (spatial EASY).
  EVERY_SYSTEM,  ///< Protect the head's projection on every system.
  GLOBAL_SHADOW, ///< Apply the earliest projection to every system.
};

/// Rule for choosing among several systems that admit a job.
enum class PlacementPolicy {
  BEST_FIT,  ///< Smallest leftover after placement; lowest index on ties.
  FIRST_FIT, ///< Lowest system index.
  WORST_FIT, ///< Largest leftover after placement; lowest index on ties.
};

/** @brief Parse a spatial backfill policy name.
 * @param[in] name One of target_only, every_system, global_shadow.
 * @return Parsed policy.
 * @throws std::invalid_argument on an unknown name. */
SpatialBackfillPolicy parse_spatial_backfill_policy(const std::string &name);

/** @brief Parse a placement policy name.
 * @param[in] name One of best_fit, first_fit, worst_fit.
 * @return Parsed policy.
 * @throws std::invalid_argument on an unknown name. */
PlacementPolicy parse_placement_policy(const std::string &name);

/** @brief Return the configuration spelling of a spatial backfill policy.
 * @param[in] policy Policy value.
 * @return Static name. */
const char *to_string(SpatialBackfillPolicy policy);

/** @brief Return the configuration spelling of a placement policy.
 * @param[in] policy Policy value.
 * @return Static name. */
const char *to_string(PlacementPolicy policy);

/** @brief One schedulable system or partition. */
struct System_Spec {
  std::string name;     ///< Unique display name.
  num_nodes_t capacity; ///< Schedulable node count.
};

/** @brief Parse a systems table with a machine,size header and comment lines.
 * @param[in] filename Path of the table.
 * @return Systems in file order.
 * @throws std::runtime_error when the file cannot be read or is malformed. */
std::vector<System_Spec> load_systems_file(const std::string &filename);

/** @brief Parse an inline systems list `name:nodes[,name:nodes...]`.
 * @param[in] text Inline specification.
 * @return Systems in list order.
 * @throws std::invalid_argument when the text is malformed. */
std::vector<System_Spec> parse_systems_inline(const std::string &text);

/** @brief Reject empty tables, duplicate names, and zero capacities.
 * @param[in] systems Systems to validate.
 * @throws std::invalid_argument when a rule is violated. */
void validate_systems(const std::vector<System_Spec> &systems);

/**
 * @brief Homogeneous node-count resource model, one integer per system.
 * @details
 * This class is the seam between the scheduling policy and the resource
 * representation. The scheduler only asks whether a job is admissible on a
 * system, what it demands there, whether it fits now, how much would be
 * left over, and when it could fit given projected releases. A heterogeneous
 * model answers the same questions with vector capacities and
 * system-dependent demands.
 */
class CountResourceModel {
public:
  /// Projected release on one system: (projected end time, demand freed).
  using release_t = std::pair<sim_time_t, num_nodes_t>;

  /** @brief Construct the model for a validated systems table.
   * @param[in] systems Systems in index order. */
  explicit CountResourceModel(std::vector<System_Spec> systems);

  /** @brief Number of systems.
   * @return System count. */
  size_t num_systems() const { return m_systems.size(); }

  /** @brief Access a system's specification.
   * @param[in] k System index.
   * @return The system's name and capacity. */
  const System_Spec &system(system_id_t k) const { return m_systems[k]; }

  /** @brief Largest capacity over all systems.
   * @return Maximum capacity. */
  num_nodes_t max_capacity() const { return m_max_capacity; }

  /** @brief Sum of capacities over all systems.
   * @return Total capacity. */
  num_nodes_t total_capacity() const { return m_total_capacity; }

  /** @brief Whether a request can ever run on system k.
   * @param[in] k System index.
   * @param[in] nodes_requested Requested node count.
   * @return true when the request does not exceed the system's capacity. */
  bool admissible(system_id_t k, num_nodes_t nodes_requested) const {
    return nodes_requested <= m_systems[k].capacity;
  }

  /** @brief Resources a request consumes on system k.
   * @param[in] k System index.
   * @param[in] nodes_requested Requested node count.
   * @return Demand on k; equal to the request in this homogeneous model. */
  num_nodes_t demand(system_id_t k, num_nodes_t nodes_requested) const {
    (void)k;
    return nodes_requested;
  }

  /** @brief Free nodes on system k given the usage recorded so far.
   * @param[in] k System index.
   * @return Capacity minus recorded usage. */
  num_nodes_t free(system_id_t k) const {
    return m_systems[k].capacity - m_used[k];
  }

  /** @brief Whether a request fits on system k right now.
   * @param[in] k System index.
   * @param[in] nodes_requested Requested node count.
   * @return true when the demand does not exceed the free nodes. */
  bool fits_now(system_id_t k, num_nodes_t nodes_requested) const {
    return demand(k, nodes_requested) <= free(k);
  }

  /** @brief Free nodes left on k after a request would start there.
   * @param[in] k System index; the request must fit now.
   * @param[in] nodes_requested Requested node count.
   * @return Leftover node count. */
  num_nodes_t leftover(system_id_t k, num_nodes_t nodes_requested) const {
    return free(k) - demand(k, nodes_requested);
  }

  /**
   * @brief Earliest time at or after now when a request fits on system k.
   * @details Starts from the free nodes on k and accumulates the projected
   * releases in time order until the demand is covered. Unlike the core's
   * single-system helper, an impossible fit returns infinity rather than the
   * current time, which is essential when the request exceeds the system.
   * @param[in] k System index.
   * @param[in] nodes_requested Requested node count.
   * @param[in,out] releases Projected releases on k; sorted in place.
   * @param[in] now Projection time.
   * @return Earliest fit time, or infinity when the request can never fit.
   */
  sim_time_t earliest_fit(system_id_t k, num_nodes_t nodes_requested,
                          std::vector<release_t> &releases,
                          sim_time_t now) const;

  /** @brief Forget all recorded usage before a pass rebuilds it. */
  void reset_usage();

  /** @brief Record that a demand is in use on system k.
   * @param[in] k System index.
   * @param[in] demand_in_use Demand to add. */
  void allocate(system_id_t k, num_nodes_t demand_in_use) {
    m_used[k] += demand_in_use;
  }

  /** @brief Total recorded usage over all systems.
   * @return Sum of per-system usage. */
  num_nodes_t total_used() const;

private:
  std::vector<System_Spec> m_systems; ///< Systems in index order.
  std::vector<num_nodes_t> m_used;    ///< Usage recorded for the current pass.
  num_nodes_t m_max_capacity;         ///< Largest single capacity.
  num_nodes_t m_total_capacity;       ///< Sum of capacities.
};

/**
 * @brief FCFS scheduler over several systems with spatial backfilling.
 * @details
 * Derives from FCFSEasyCustomScheduler only so that it can be injected through
 * the existing `BasicSimulation(Sim_Params,
 * std::unique_ptr<FCFSEasyCustomScheduler>)` constructor. Every virtual of the
 * scheduler interface is overridden here; the inherited circular buffer and
 * resource accounting stay unused.
 *
 * The scheduler requires `BackfillPolicy::EASY` (spatial backfilling on) or
 * `BackfillPolicy::NONE` (strict FCFS over all systems). Jobs larger than
 * the largest system are rejected in insert_job() because the core only
 * rejects jobs larger than the summed capacity.
 */
class MultiSystemBackfillScheduler : public FCFSEasyCustomScheduler {
public:
  /// Where a started job was placed and what it consumes there.
  struct Placement {
    system_id_t system; ///< Index of the system the job runs on.
    num_nodes_t demand; ///< Demand charged on that system.
  };

  /**
   * @brief Construct the scheduler for a validated systems table.
   * @param[in] systems Systems in index order.
   * @param[in] spatial_policy Window rule for backfill candidates.
   * @param[in] placement_policy Rule for choosing among admitting systems.
   * @param[in] bf_policy EASY enables backfilling; NONE disables it.
   * @param[in] initial_job_count Known job count; sizes nothing that is used
   * here but is forwarded to the base class.
   * @throws std::invalid_argument when bf_policy is CONSERVATIVE or the
   * systems table is invalid.
   */
  MultiSystemBackfillScheduler(std::vector<System_Spec> systems,
                               SpatialBackfillPolicy spatial_policy,
                               PlacementPolicy placement_policy,
                               BackfillPolicy bf_policy,
                               size_t initial_job_count);

  /** @copydoc SchedulerBase::insert_job
   * @throws std::logic_error when the request exceeds every system; the core
   * is expected to have consulted can_ever_run() first. */
  void insert_job(job_no_t job_id, sim_time_t submit_time,
                  tdiff_t run_time_estimate,
                  num_nodes_t nodes_requested) override;

  /** @copydoc SchedulerBase::can_ever_run
   * @details Declines requests larger than the largest system, so the core
   * rejects them at submission instead of queueing them forever. */
  bool can_ever_run(num_nodes_t nodes_requested) const override {
    return nodes_requested <= m_model.max_capacity();
  }

  /** @copydoc SchedulerBase::schedule
   * @throws std::logic_error when a running job has no recorded placement or
   * the per-system usage disagrees with the caller's free-node count. */
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
  sim_time_t get_next_arrival_time() override;

  /** @copydoc SchedulerBase::has_eligible_jobs */
  bool has_eligible_jobs() override { return active_job_count() > 0; }

  /** @copydoc SchedulerBase::pending_job_ids */
  std::vector<job_no_t> pending_job_ids() const override;

  /** @brief Placement of every job this scheduler has started.
   * @return Map from job identifier to placement. */
  const std::unordered_map<job_no_t, Placement> &placements() const {
    return m_placements;
  }

  /** @brief The resource model, including the systems table.
   * @return Model reference. */
  const CountResourceModel &model() const { return m_model; }

  /** @brief Configured window rule.
   * @return Spatial backfill policy. */
  SpatialBackfillPolicy spatial_policy() const { return m_spatial_policy; }

  /** @brief Configured system choice rule.
   * @return Placement policy. */
  PlacementPolicy placement_policy() const { return m_placement_policy; }

  /** @brief System holding the current head reservation, if any.
   * @return System index, or num_systems() when no head is blocked. */
  system_id_t reserved_system() const { return m_reserved_system; }

  /** @brief Head projections per system from the latest pass.
   * @return One projected start per system; infinity where inadmissible. */
  const std::vector<sim_time_t> &shadow_times() const { return m_shadow_times; }

  /** @brief Jobs started on each system so far.
   * @return Per-system start counts. */
  const std::vector<size_t> &jobs_started_by_system() const {
    return m_jobs_started_by_system;
  }

protected:
  /** @copydoc SchedulerBase::wait_queue_size */
  size_t wait_queue_size() const override { return m_wait_queue.size(); }

  /** @copydoc SchedulerBase::waiting_resource_area */
  std::optional<tdiff_t> waiting_resource_area() const override;

private:
  /// Wait-queue entry in submit order.
  struct JobEntry {
    job_no_t job_id;             ///< Trace job identifier.
    sim_time_t submit_time;      ///< Arrival time.
    tdiff_t run_time_estimate;   ///< Time limit used for projections.
    num_nodes_t nodes_requested; ///< Requested node count.
    bool removed;                ///< True once started; dropped lazily.

    /** @brief Initialize an unscheduled entry.
     * @param[in] id Trace job identifier.
     * @param[in] submit Arrival time.
     * @param[in] run_time Time limit.
     * @param[in] nodes Requested node count. */
    JobEntry(job_no_t id, sim_time_t submit, tdiff_t run_time,
             num_nodes_t nodes)
        : job_id(id), submit_time(submit), run_time_estimate(run_time),
          nodes_requested(nodes), removed(false) {}
  };

  /** @brief Choose one system among candidates by the placement policy.
   * @param[in] nodes_requested Requested node count.
   * @param[in] candidates Admitting systems in ascending index order.
   * @return Chosen system index. */
  system_id_t choose_system(num_nodes_t nodes_requested,
                            const std::vector<system_id_t> &candidates) const;

  /** @brief Record a start on system k and charge the model.
   * @param[in] job Queue entry being started.
   * @param[in] k Chosen system.
   * @param[in] current_time Start time.
   * @param[in,out] releases Per-system projected releases to extend. */
  void place(const JobEntry &job, system_id_t k, sim_time_t current_time,
             std::vector<std::vector<CountResourceModel::release_t>> &releases);

  /** @brief Drop removed entries once they dominate the queue. */
  void compact_if_needed();

  std::deque<JobEntry> m_wait_queue;      ///< Submit-ordered queue.
  size_t m_eligible_end_idx;              ///< End of the eligible prefix.
  sim_time_t m_current_tracked_time;      ///< Latest synchronized time.
  size_t m_removed_count;                 ///< Removed entries in the prefix.
  CountResourceModel m_model;             ///< Per-system resource model.
  SpatialBackfillPolicy m_spatial_policy; ///< Window rule.
  PlacementPolicy m_placement_policy;     ///< System choice rule.
  std::unordered_map<job_no_t, Placement> m_placements; ///< Started jobs.
  std::vector<sim_time_t> m_shadow_times; ///< Head projections per system.
  system_id_t m_reserved_system;          ///< System of the head reservation.
  std::vector<size_t> m_jobs_started_by_system; ///< Start counts.
};

} // namespace experimental
} // namespace dr_evt

#endif // DR_EVT_EXPERIMENTAL_MULTI_SYSTEM_BACKFILL_SCHEDULER_HPP
