/******************************************************************************
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
/** @file scheduler_placement_circular.hpp
 * @brief Exact placement through the main simulator's existing circular FCFS
 * queue.
 */
#ifndef DR_EVT_SIM_SCHEDULER_PLACEMENT_CIRCULAR_HPP
#define DR_EVT_SIM_SCHEDULER_PLACEMENT_CIRCULAR_HPP
#include "sim/resource/reservation_profile.hpp"
#include "sim/scheduler_fcfs_hybrid_circular.hpp"
#include <ranges>
namespace dr_evt {
/** @brief FCFS allocation and exact reservation traversal selected at
 * construction.
 * @tparam Model Stateless placement operations, observer, and compile-time
 * optimizations.
 * @tparam OneSecondEventGroups Whether future fits use inclusive OpenPBS
 * one-second event groups instead of every release time.
 * @details The existing queue owns pending metadata. Returned decisions own
 * selected masks until the simulation moves them into its running store. Vector
 * capacity is bounded before borrowing, so selected allocation addresses remain
 * stable throughout planning. Only owned future masks survive for reporting.
 * A positive backfill interval selects a default-Slurm-style timer pass whose
 * profile contains only waiting-job bookings; running jobs contribute forecast
 * releases in widening clock-aligned groups. Event dispatch does not reuse the
 * retained timer plan's node identities. */
template <typename Model, bool OneSecondEventGroups = false>
class PlacementCircularScheduler : public FCFSHybridCircularScheduler {
  std::optional<typename Model::Timeline>
      m_last_plan;                    ///< Latest owned reservation snapshot.
  ResourceDiagnostics *m_diagnostics; ///< Borrowed simulation-owned counters.
  bool m_periodic_backfill; ///< Slurm-style timer backfill: flat-topology run
                            ///< best fit, inclusive backfill endpoints, and a
                            ///< reported plan kept until the next timer pass.

  sim_time_t m_profile_cycle_time =
      std::numeric_limits<sim_time_t>::quiet_NaN(); ///< Last timer cycle.

  /** @brief Remove queue entries started by an earlier pass. */
  void compact_queue() {
    if (m_removed_count == 0)
      return;
    m_eligible_end_idx -= m_removed_count;
    auto end = std::remove_if(m_wait_queue.begin(), m_wait_queue.end(),
                              [](const JobEntry &job) { return job.removed; });
    m_wait_queue.erase(end, m_wait_queue.end());
    m_removed_count = 0;
  }

  /**
   * @brief One periodic backfill cycle with a waiting-jobs-only profile.
   * @details Matches default Slurm (no bf_running_job_reserve): the profile
   * holds only bookings made this cycle; future starts come from releasing
   * running jobs in widening, clock-aligned groups. Queue heads that fit on
   * idle nodes start first, as main dispatch would. One cycle runs per timer
   * time; repeated same-time calls return no further starts.
   * @param[in] live Exact running allocations and eligible-node inventory.
   * @param[in] current_time Timer-pass timestamp.
   * @return Owning exact decisions selected for immediate execution.
   * @throws std::invalid_argument if bitmap domains or planning intervals are
   * inconsistent.
   * @throws std::bad_alloc if queue, bitmap, or reservation storage fails.
   */
  std::vector<Placement_Decision>
  schedule_profile_cycle(const running_jobs_t::PlacementState &live,
                         sim_time_t current_time) {
    std::vector<Placement_Decision> starts;
    if (m_profile_cycle_time == current_time)
      return starts;
    m_profile_cycle_time = current_time;
    m_backfill_depth_time = current_time;
    m_backfill_depth_examined = 0;
    m_backfill_depth_head_accounted = false;
    m_fcfs_reservation_time = std::numeric_limits<sim_time_t>::max();
    m_last_plan.reset();
    constexpr auto order = Model::order;
    const auto select = [](const Bitmap &candidates, num_nodes_t quantity) {
      return *candidates.template select_best_fit_runs<order>(quantity);
    };
    Bitmap idle = live.eligible;
    idle.remove(live.occupied);
    std::vector<ReleaseRecord> running;
    running.reserve(live.jobs.size());
    for (const auto &[id, record] : live.jobs) {
      (void)id;
      const auto limit_end = record.timing.start_time + record.timing.run_time;
      // Past-forecast jobs keep their nodes until an observed release.
      if (limit_end > current_time)
        running.push_back({limit_end, record.allocation});
    }
    const auto start_now = [&](JobEntry &job, Bitmap nodes, tdiff_t duration) {
      idle.remove(nodes);
      running.push_back({current_time + duration, nodes});
      starts.push_back({job.job_id, current_time + duration, std::move(nodes)});
      job.removed = true;
      ++m_removed_count;
    };
    // Main-dispatch heads first, by best fit over idle nodes.
    size_t cursor = 0;
    for (; cursor < m_eligible_end_idx; ++cursor) {
      auto &job = m_wait_queue[cursor];
      if (job.nodes_requested > idle.count())
        break;
      start_now(job, select(idle, job.nodes_requested),
                release_estimate(current_time, job.run_time_estimate));
    }
    std::ranges::stable_sort(running, {}, &ReleaseRecord::limit_end);
    const sim_time_t until =
        m_planning_horizon == std::numeric_limits<tdiff_t>::max()
            ? std::numeric_limits<sim_time_t>::infinity()
            : current_time + m_planning_horizon;
    ReservationProfile profile(live.eligible, current_time, until);
    typename Model::Timeline report(live.eligible, current_time);
    const size_t reservation_limit =
        m_backfill_policy == BackfillPolicy::CONSERVATIVE
            ? std::numeric_limits<size_t>::max()
        : m_backfill_policy == BackfillPolicy::EASY ? 1
                                                    : m_reservation_depth;
    size_t booked = 0;
    bool first_waiter = true;
    for (; cursor < m_eligible_end_idx; ++cursor) {
      if (m_backfill_queue_depth != 0 &&
          m_backfill_depth_examined >= m_backfill_queue_depth)
        break;
      auto &job = m_wait_queue[cursor];
      ++m_backfill_depth_examined;
      const auto duration =
          release_estimate(current_time, job.run_time_estimate);
      const bool now_only = booked >= reservation_limit;
      const auto pick = [&](const Bitmap &candidates) {
        return select(candidates, job.nodes_requested);
      };
      auto plan = plan_profile_job(profile, running, idle, job.nodes_requested,
                                   duration, current_time, now_only, pick);
      if (plan && plan->first <= current_time) {
        profile.book(current_time, current_time + duration, plan->second);
        // Keep the sorted release order for later candidates.
        start_now(job, std::move(plan->second), duration);
        // Insert the appended release after equal limit ends, matching a
        // stable sort of the otherwise sorted records.
        const auto last = std::prev(running.end());
        std::rotate(std::ranges::upper_bound(running.begin(), last,
                                             last->limit_end, {},
                                             &ReleaseRecord::limit_end),
                    last, running.end());
        continue;
      }
      const bool was_first = first_waiter;
      first_waiter = false;
      if (!plan || now_only)
        continue;
      if (was_first)
        m_fcfs_reservation_time = plan->first;
      record_estimated_start(
          job.job_id, plan->first,
          reservation_within_horizon(plan->first, current_time));
      profile.book(plan->first, plan->first + duration, plan->second);
      report.reserve(BitmapPlacement{plan->first, std::move(plan->second)},
                     duration, job.job_id);
      ++booked;
    }
    m_backfill_depth_head_accounted = m_backfill_depth_examined != 0;
    m_last_plan.emplace(std::move(report));
    return starts;
  }

  /**
   * @brief Run the shared exact-placement traversal.
   * @param[in] free_nodes Nodes currently available for allocation.
   * @param[in] running_jobs Jobs already running with exact allocations.
   * @param[in] current_time Current scheduling time.
   * @param[in] backfill Whether to examine jobs beyond the first waiter.
   * @return Exact decisions selected for immediate execution.
   */
  std::vector<Placement_Decision>
  schedule_placements_impl(num_nodes_t free_nodes,
                           const running_jobs_t &running_jobs,
                           sim_time_t current_time, bool backfill) {
    const auto *live = running_jobs.placement();
    if (!live || free_nodes != live->eligible.count() - live->occupied.count())
      throw std::invalid_argument("placement and trace availability disagree");
    sync_to(current_time);
    if (m_periodic_backfill && backfill) {
      compact_queue();
      return schedule_profile_cycle(*live, current_time);
    }
    // Event dispatch between timer passes keeps the last backfill plan for
    // status reporting only; selection never consults its identities.
    const bool retain_periodic_plan = m_periodic_backfill && !backfill;
    if (!retain_periodic_plan)
      m_last_plan.reset();
    if (m_removed_count != 0) {
      m_eligible_end_idx -= m_removed_count;
      auto end =
          std::remove_if(m_wait_queue.begin(), m_wait_queue.end(),
                         [](const JobEntry &job) { return job.removed; });
      m_wait_queue.erase(end, m_wait_queue.end());
      m_removed_count = 0;
    }
    std::vector<Placement_Decision> starts;
    starts.reserve(
        std::min(active_job_count(), static_cast<size_t>(free_nodes)));
    Bitmap projected_occupied = live->occupied;
    std::optional<typename Model::Timeline> plan;
    const auto ensure_plan = [&] {
      if (plan)
        return;
      auto eligible = live->eligible;
      // Jobs still executing or cleaning up after their forecast expires are
      // unavailable until an observed release. Do not invent a release time.
      for (const auto &[id, record] : live->jobs) {
        (void)id;
        if (record.timing.start_time + record.timing.run_time <= current_time)
          eligible.remove(record.allocation);
      }
      plan.emplace(std::move(eligible), current_time);
      // Slurm backfill requires capacity at each candidate's requested end.
      plan->set_inclusive_end(m_periodic_backfill && backfill);
      auto future = std::ranges::filter_view(
          std::views::all(live->jobs), [current_time](const auto &entry) {
            return entry.second.timing.start_time +
                       entry.second.timing.run_time >
                   current_time;
          });
      if constexpr (Model::batch_seeding) {
        plan->seed_running(future, [current_time](const auto &entry) {
          const auto &record = entry.second;
          return PlacementInterval{
              current_time, record.timing.start_time + record.timing.run_time,
              entry.first, std::cref(record.allocation)};
        });
      } else {
        for (const auto &[id, record] : future)
          plan->borrow_running(
              record.allocation,
              record.timing.start_time + record.timing.run_time, id);
      }
      for (const auto &selected : starts)
        plan->borrow_running(selected.allocation, selected.forecast_finish,
                             selected.job);
    };
    if constexpr (!Model::immediate_fast_path)
      ensure_plan();
    const size_t reservation_limit =
        m_backfill_policy == BackfillPolicy::CONSERVATIVE
            ? std::numeric_limits<size_t>::max()
        : m_backfill_policy == BackfillPolicy::EASY   ? 1
        : m_backfill_policy == BackfillPolicy::HYBRID ? m_reservation_depth
                                                      : 0;
    size_t reserved = 0;
    if (backfill && m_backfill_depth_time != current_time) {
      m_backfill_depth_time = current_time;
      m_backfill_depth_examined = 0;
      m_backfill_depth_head_accounted = false;
    }
    m_fcfs_reservation_time = std::numeric_limits<sim_time_t>::max();
    bool waiting_head = false;
    for (size_t cursor = 0; cursor < m_eligible_end_idx; ++cursor) {
      auto &job = m_wait_queue[cursor];
      if (backfill && m_backfill_queue_depth != 0 && waiting_head &&
          m_backfill_depth_examined >= m_backfill_queue_depth)
        break;
      const auto duration =
          release_estimate(current_time, job.run_time_estimate);
      ResourceContext context{job.job_id, current_time, 0, nullptr,
                              m_diagnostics};
      std::optional<typename Model::Placement> placement;
      if constexpr (Model::immediate_fast_path) {
        if (!plan) {
          if (m_periodic_backfill)
            placement = Model::find_available_best_fit(
                live->eligible, projected_occupied, job.nodes_requested,
                duration, current_time, context);
          else
            placement = Model::find_available(
                live->eligible, projected_occupied, job.nodes_requested,
                duration, current_time, context);
        }
      }
      if (!placement && (plan || reserved < reservation_limit)) {
        ensure_plan();
        if (reserved < reservation_limit) {
          if constexpr (OneSecondEventGroups)
            placement = Model::find_fit_1s_event_group(
                *plan, job.nodes_requested, duration, current_time, context);
          else {
            placement = Model::find_fit(*plan, job.nodes_requested, duration,
                                        current_time, context);
            // Slurm selects nodes free at the chosen start by flat-topology
            // run best fit, for immediate starts and future reservations.
            if (placement && m_periodic_backfill)
              if (auto best = Model::find_at_best_fit(
                      *plan, job.nodes_requested, duration, placement->start,
                      context))
                placement = std::move(best);
          }
        } else if (Model::can_fit(*plan, job.nodes_requested, duration,
                                  current_time, context)) {
          if (m_periodic_backfill)
            placement = Model::find_at_best_fit(
                *plan, job.nodes_requested, duration, current_time, context);
          else
            placement = Model::find_at(*plan, job.nodes_requested, duration,
                                       current_time, context);
        }
      }
      if (placement && placement->start == current_time) {
        starts.push_back({job.job_id, current_time + duration,
                          std::move(placement->allocation)});
        projected_occupied.add(starts.back().allocation);
        if (plan)
          plan->borrow_running(starts.back().allocation,
                               starts.back().forecast_finish, job.job_id);
        job.removed = true;
        ++m_removed_count;
        if (backfill && waiting_head)
          ++m_backfill_depth_examined;
      } else {
        const bool first_waiter = !waiting_head;
        waiting_head = true;
        if (placement) {
          if (first_waiter)
            m_fcfs_reservation_time = placement->start;
          const bool protected_start =
              reservation_within_horizon(placement->start, current_time);
          record_estimated_start(job.job_id, placement->start, protected_start);
          if (protected_start) {
            Model::reserve(*plan, std::move(*placement), job.nodes_requested,
                           duration, context);
            ++reserved;
          }
        }
        if (backfill && (!first_waiter || !m_backfill_depth_head_accounted)) {
          ++m_backfill_depth_examined;
          m_backfill_depth_head_accounted = true;
        }
        if (!backfill || m_backfill_policy == BackfillPolicy::NONE)
          break;
      }
    }
    if (plan && !retain_periodic_plan) {
      plan->discard_borrowed();
      m_last_plan.emplace(std::move(*plan));
    }
    return starts;
  }

public:
  /** @brief Construct one compile-time placement specialization.
   * @param[in] total_nodes Physical inventory size.
   * @param[in] initial_jobs Initial queue sizing hint.
   * @param[in] params Queue, backfill, and examination settings.
   * @param[in,out] diagnostics Optional observer owner, which outlives this
   * scheduler. */
  PlacementCircularScheduler(num_nodes_t total_nodes, size_t initial_jobs,
                             const Sim_Params &params,
                             ResourceDiagnostics *diagnostics)
      : FCFSHybridCircularScheduler(
            total_nodes, initial_jobs,
            params.m_backfill_policy == BackfillPolicy::EASY
                ? BackfillPolicy::HYBRID
                : params.m_backfill_policy,
            params.m_wait_queue_capacity, params.m_wait_queue_overflow,
            params.m_reservation_depth, params.m_backfill_queue_depth),
        m_diagnostics(diagnostics),
        m_periodic_backfill(params.m_backfill_interval > 0) {
    m_backfill_policy = params.m_backfill_policy;
  }
  /** @brief Reject empty and oversized whole-node requests before queue
   * admission.
   * @param[in] nodes_requested Requested exclusive node count.
   * @return True for a positive quantity within the static inventory. */
  bool can_ever_run(num_nodes_t nodes_requested) const override {
    return nodes_requested > 0 && nodes_requested <= m_total_nodes;
  }
  /** @copydoc SchedulerBase::placement_reservations */
  std::span<const PlacementInterval> placement_reservations() const override {
    return m_last_plan ? m_last_plan->intervals()
                       : std::span<const PlacementInterval>{};
  }
  /** @copydoc SchedulerBase::placement_budget */
  std::pair<sim_time_t, size_t> placement_budget() const override {
    return {m_backfill_depth_time, m_backfill_depth_examined};
  }
  /** @copydoc SchedulerBase::restore_placement_budget
   * @details Restoring a nonzero periodic budget also marks that timestamp's
   * profile cycle consumed, preventing a duplicate pass after restart. */
  void restore_placement_budget(std::pair<sim_time_t, size_t> budget) override {
    m_backfill_depth_time = budget.first;
    m_backfill_depth_examined = budget.second;
    m_backfill_depth_head_accounted = budget.second != 0;
    if (m_periodic_backfill && budget.second != 0)
      m_profile_cycle_time = budget.first;
  }
  /** @copydoc SchedulerBase::schedule_placements
   * @throws std::invalid_argument if live bitmap and quantity accounting
   * differ.
   * @throws std::bad_alloc if queue, decision, or planner storage cannot be
   * allocated. */
  std::vector<Placement_Decision>
  schedule_placements(num_nodes_t free_nodes,
                      const running_jobs_t &running_jobs,
                      sim_time_t current_time) override {
    return schedule_placements_impl(free_nodes, running_jobs, current_time,
                                    true);
  }

  /** @copydoc SchedulerBase::schedule_placements_event */
  std::vector<Placement_Decision>
  schedule_placements_event(num_nodes_t free_nodes,
                            const running_jobs_t &running_jobs,
                            sim_time_t current_time) override {
    return schedule_placements_impl(free_nodes, running_jobs, current_time,
                                    false);
  }
};
} // namespace dr_evt
#endif
