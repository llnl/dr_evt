/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_base.cpp
 * @brief Shared capacity forecasts and scheduler selection.
 * @details Forecasts subtract requested capacity over complete reservation
 * intervals. The factory chooses queue order, backfill protection,
 * examination accounting, and timing. The simulation driver owns event and
 * timer wakes; this shared code is not a standalone scheduler.
 * @note CLI: Concrete selection uses --priority_policy, --backfill_policy,
 * --queue_impl, --per_pass_accounting, --backfill_interval, and
 * --dispatch_interval. --reservation_depth and the backfill/dispatch queue
 * depths configure the chosen policy. See the concrete scheduler file for a
 * complete supported combination.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#include "sim/scheduler_base.hpp"
#include "sim/scheduler_fcfs_1s_evt_group_circular.hpp"
#include "sim/scheduler_fcfs_easy_block.hpp"
#include "sim/scheduler_fcfs_easy_circular.hpp"
#include "sim/scheduler_fcfs_easy_deque.hpp"
#include "sim/scheduler_fcfs_easy_multimap.hpp"
#include "sim/scheduler_fcfs_hybrid_circular.hpp"
#include "sim/scheduler_fcfs_hybrid_deque.hpp"
#include "sim/scheduler_fcfs_pass_hybrid_circular.hpp"
#include "sim/scheduler_fcfs_periodic_easy_circular.hpp"
#include "sim/scheduler_fcfs_periodic_hybrid_circular.hpp"
#include "sim/scheduler_ljf_easy.hpp"
#include "sim/scheduler_periodic_fcfs_hybrid_circular.hpp"
#include "sim/scheduler_sjf_easy.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace dr_evt {

tdiff_t SchedulerBase::prediction_horizon(const running_jobs_t &running_jobs,
                                          sim_time_t current_time,
                                          double utilization) const {
  if (m_backfill_policy != BackfillPolicy::EASY) {
    throw std::logic_error("prediction horizon requires EASY backfilling");
  }
  if (!std::isfinite(utilization) || utilization < 0.0 || utilization > 1.0) {
    throw std::invalid_argument("utilization must be finite and in [0, 1]");
  }

  const auto queued_area = waiting_resource_area();
  if (!queued_area.has_value()) {
    throw std::logic_error(
        "prediction horizon is unsupported by this scheduler");
  }
  if (*queued_area <= 0.0) {
    return 0.0;
  }
  if (m_total_nodes == 0) {
    return std::numeric_limits<tdiff_t>::infinity();
  }

  const double effective_utilization = utilization == 0.0 ? 1.0 : utilization;
  const sim_time_t shadow_time =
      std::max(current_time, m_fcfs_reservation_time);
  double available_nodes = static_cast<double>(m_total_nodes);
  std::map<sim_time_t, num_nodes_t> releases_by_time;
  for (const auto &[job_id, job] : running_jobs) {
    (void)job_id;
    const sim_time_t end_time = job.start_time + job.run_time;
    if (end_time > shadow_time) {
      available_nodes -= static_cast<double>(job.nodes);
      releases_by_time[end_time] += job.nodes;
    }
  }

  tdiff_t usable_area = 0.0;
  sim_time_t previous_time = shadow_time;
  for (const auto &[release_time, nodes_released] : releases_by_time) {
    usable_area += effective_utilization * available_nodes *
                   (release_time - previous_time);
    if (usable_area >= *queued_area) {
      return release_time - shadow_time;
    }
    available_nodes += static_cast<double>(nodes_released);
    previous_time = release_time;
  }

  return previous_time - shadow_time +
         (*queued_area - usable_area) /
             (effective_utilization * static_cast<double>(m_total_nodes));
}

sim_time_t SchedulerBase::calculate_fcfs_reservation(
    num_nodes_t nodes_needed, num_nodes_t free_nodes,
    const running_jobs_t &running_jobs, sim_time_t current_time,
    const projected_starts_t &projected_starts) {
  const auto reservation = reservation_timeline(free_nodes, running_jobs,
                                                current_time, projected_starts)
                               .earliest_fit(nodes_needed, 0.0, current_time);
  return reservation.value_or(std::numeric_limits<sim_time_t>::max());
}

Reservation_Timeline SchedulerBase::reservation_timeline(
    num_nodes_t free_nodes, const running_jobs_t &running_jobs,
    sim_time_t current_time, const projected_starts_t &projected_starts) const {
  std::vector<std::pair<sim_time_t, std::int64_t>> deltas;
  deltas.reserve(running_jobs.size() + projected_starts.size());
  for (const auto &[job_idx, job] : running_jobs) {
    (void)job_idx;
    const sim_time_t end_time = job.start_time + job.run_time;
    if (end_time > current_time) {
      deltas.emplace_back(end_time, static_cast<std::int64_t>(job.nodes));
    }
  }
  for (const auto &job : projected_starts) {
    const sim_time_t end_time = job.start_time + job.run_time;
    if (end_time > current_time) {
      deltas.emplace_back(end_time, static_cast<std::int64_t>(job.nodes));
    }
  }

  // Signed running balance of free nodes. After a capacity reduction the
  // system may be overcommitted: free_nodes is then 0 while the true slack is
  // negative, and releases must first pay that deficit back.
  std::int64_t balance = static_cast<std::int64_t>(free_nodes);
  if (free_nodes == 0 && m_outlook_occupied > m_outlook_capacity) {
    balance = static_cast<std::int64_t>(m_outlook_capacity) -
              static_cast<std::int64_t>(m_outlook_occupied);
  }
  // Scheduled changes are absolute capacities; walk them as deltas from the
  // capacity in effect now. Changes already due are skipped (the simulation
  // applies them before scheduling) but still move the reference capacity.
  const Capacity_Change *change = m_outlook_changes_begin;
  num_nodes_t reference_capacity = m_outlook_capacity;
  while (change != m_outlook_changes_end && change->time <= current_time) {
    reference_capacity = change->total_nodes;
    ++change;
  }

  while (change != m_outlook_changes_end) {
    if (change->time > current_time) {
      deltas.emplace_back(change->time,
                          static_cast<std::int64_t>(change->total_nodes) -
                              static_cast<std::int64_t>(reference_capacity));
    }
    reference_capacity = change->total_nodes;
    ++change;
  }
  const auto clamp = [](std::int64_t value) {
    return value > 0 ? static_cast<num_nodes_t>(value) : num_nodes_t{0};
  };
  std::sort(deltas.begin(), deltas.end());
  std::vector<std::pair<sim_time_t, num_nodes_t>> profile;
  profile.reserve(deltas.size() + 1);
  profile.emplace_back(current_time, clamp(balance));
  for (size_t begin = 0; begin < deltas.size();) {
    const sim_time_t time = deltas[begin].first;
    std::int64_t delta = 0;
    do {
      delta += deltas[begin].second;
      ++begin;
    } while (begin < deltas.size() && deltas[begin].first == time);
    balance += delta;
    profile.emplace_back(time, clamp(balance));
  }
  return Reservation_Timeline(std::move(profile));
}

namespace {
/** @brief Return the configuration spelling of a queue implementation.
 * @param[in] impl Queue implementation value.
 * @return Static null-terminated configuration name. */
const char *queue_impl_name(QueueImplementation impl) {
  switch (impl) {
  case QueueImplementation::BLOCK:
    return "block";
  case QueueImplementation::CIRCULAR:
    return "circular";
  case QueueImplementation::MULTIMAP:
    return "multimap";
  case QueueImplementation::DEQUE:
    return "deque";
  default:
    return "multimap";
  }
}

std::unique_ptr<SchedulerBase> create_hybrid_scheduler(
    num_nodes_t total_nodes, size_t initial_job_count,
    BackfillPolicy backfill_policy, QueueImplementation queue_impl,
    size_t wait_queue_capacity, CircularOverflowPolicy wait_queue_overflow,
    size_t reservation_depth, size_t backfill_queue_depth) {
  if (queue_impl == QueueImplementation::DEQUE) {
    return std::make_unique<FCFSHybridDequeScheduler>(
        total_nodes, backfill_policy, reservation_depth, backfill_queue_depth);
  }
  if (queue_impl != QueueImplementation::CIRCULAR) {
    std::cerr << "Warning: queue_impl '" << queue_impl_name(queue_impl)
              << "' not supported for FCFS_CONSERVATIVE, using circular\n";
  }
  return std::make_unique<FCFSHybridCircularScheduler>(
      total_nodes, initial_job_count, backfill_policy, wait_queue_capacity,
      wait_queue_overflow, reservation_depth, backfill_queue_depth);
}
} // anonymous namespace

std::unique_ptr<SchedulerBase>
create_scheduler(num_nodes_t total_nodes, size_t initial_job_count,
                 BackfillPolicy backfill_policy, PriorityPolicy priority_policy,
                 QueueImplementation queue_impl, size_t block_size,
                 size_t wait_queue_capacity,
                 CircularOverflowPolicy wait_queue_overflow,
                 size_t reservation_depth, size_t backfill_queue_depth,
                 tdiff_t backfill_interval, tdiff_t dispatch_interval,
                 size_t dispatch_queue_depth, bool dispatch_on_events,
                 bool per_pass_accounting, bool diagnostic_no_reservations) {
  if (priority_policy == PriorityPolicy::FCFS_1S_EVT_GROUP) {
    if (dispatch_interval != 0.0 || backfill_interval != 0.0 ||
        dispatch_queue_depth != 0 || dispatch_on_events ||
        diagnostic_no_reservations ||
        queue_impl != QueueImplementation::CIRCULAR) {
      throw std::invalid_argument("fcfs_1s_evt_group requires event-driven "
                                  "scheduling, queue_impl=circular, "
                                  "and protected reservations");
    }
    return std::make_unique<FCFSOneSecondEventGroupCircularScheduler>(
        total_nodes, initial_job_count, backfill_policy, wait_queue_capacity,
        wait_queue_overflow, reservation_depth, backfill_queue_depth);
  }
  if (diagnostic_no_reservations) {
    if (backfill_interval <= 0.0 || !std::isfinite(backfill_interval) ||
        dispatch_interval != 0.0 || dispatch_queue_depth != 0 ||
        dispatch_on_events || per_pass_accounting ||
        queue_impl != QueueImplementation::CIRCULAR ||
        (priority_policy != PriorityPolicy::FCFS &&
         priority_policy != PriorityPolicy::FCFS_CONSERVATIVE) ||
        (backfill_policy != BackfillPolicy::HYBRID &&
         backfill_policy != BackfillPolicy::CONSERVATIVE) ||
        (backfill_policy == BackfillPolicy::HYBRID && reservation_depth == 0)) {
      throw std::invalid_argument(
          "diagnostic_no_reservations requires event-driven FCFS dispatch "
          "with periodic hybrid/conservative backfill and queue_impl=circular");
    }
    return std::make_unique<FCFSPeriodicNoReservationCircularScheduler>(
        total_nodes, initial_job_count, wait_queue_capacity,
        wait_queue_overflow, backfill_policy, reservation_depth,
        backfill_queue_depth);
  }
  if (per_pass_accounting) {
    if (dispatch_interval != 0.0 || backfill_interval != 0.0 ||
        dispatch_queue_depth != 0 || dispatch_on_events) {
      throw std::invalid_argument(
          "per_pass_accounting requires event-driven scheduling");
    }
    if ((priority_policy != PriorityPolicy::FCFS &&
         priority_policy != PriorityPolicy::FCFS_CONSERVATIVE) ||
        queue_impl != QueueImplementation::CIRCULAR) {
      throw std::invalid_argument(
          "per_pass_accounting requires FCFS ordering and queue_impl=circular");
    }
    if (backfill_policy == BackfillPolicy::HYBRID && reservation_depth == 0) {
      throw std::invalid_argument(
          "hybrid backfilling requires reservation_depth > 0");
    }
    return std::make_unique<FCFSPassHybridCircularScheduler>(
        total_nodes, initial_job_count, backfill_policy, wait_queue_capacity,
        wait_queue_overflow, reservation_depth, backfill_queue_depth);
  }
  if (dispatch_on_events && dispatch_interval <= 0.0) {
    throw std::invalid_argument(
        "dispatch_on_events requires dispatch_interval > 0");
  }
  if (!std::isfinite(dispatch_interval) || dispatch_interval < 0.0) {
    throw std::invalid_argument(
        "dispatch_interval must be finite and nonnegative");
  }
  if (dispatch_interval > 0.0) {
    if (backfill_interval != 0.0) {
      throw std::invalid_argument(
          "dispatch_interval and backfill_interval are mutually exclusive");
    }
    if (priority_policy != PriorityPolicy::FCFS &&
        priority_policy != PriorityPolicy::FCFS_CONSERVATIVE) {
      throw std::invalid_argument(
          "fixed periodic dispatch requires FCFS ordering");
    }
    if (queue_impl != QueueImplementation::CIRCULAR) {
      throw std::invalid_argument(
          "fixed periodic dispatch requires queue_impl=circular");
    }
    if (backfill_queue_depth != 0) {
      throw std::invalid_argument(
          "use dispatch_queue_depth instead of backfill_queue_depth for fixed "
          "periodic dispatch");
    }
    if (backfill_policy == BackfillPolicy::HYBRID && reservation_depth == 0) {
      throw std::invalid_argument(
          "hybrid backfilling requires reservation_depth > 0");
    }
    return std::make_unique<PeriodicFCFSHybridCircularScheduler>(
        total_nodes, initial_job_count, backfill_policy, wait_queue_capacity,
        wait_queue_overflow, reservation_depth, dispatch_queue_depth);
  }
  if (dispatch_queue_depth != 0) {
    throw std::invalid_argument(
        "dispatch_queue_depth requires dispatch_interval > 0");
  }
  if (!std::isfinite(backfill_interval) || backfill_interval < 0.0) {
    throw std::invalid_argument(
        "backfill_interval must be finite and nonnegative");
  }
  if (backfill_interval > 0.0) {
    if (backfill_policy == BackfillPolicy::NONE) {
      throw std::invalid_argument(
          "periodic backfilling requires an enabled backfill policy");
    }
    if (priority_policy != PriorityPolicy::FCFS &&
        priority_policy != PriorityPolicy::FCFS_CONSERVATIVE) {
      throw std::invalid_argument(
          "periodic backfilling is supported with FCFS ordering only");
    }
    if (queue_impl != QueueImplementation::CIRCULAR) {
      throw std::invalid_argument(
          "periodic backfilling requires queue_impl=circular");
    }
    if (backfill_policy == BackfillPolicy::HYBRID && reservation_depth == 0) {
      throw std::invalid_argument(
          "hybrid backfilling requires reservation_depth > 0");
    }
    if (backfill_policy == BackfillPolicy::EASY) {
      return std::make_unique<FCFSPeriodicEasyCircularScheduler>(
          total_nodes, initial_job_count, wait_queue_capacity,
          wait_queue_overflow, backfill_queue_depth);
    }
    // Periodic hybrid reserves a fixed number of jobs; conservative uses an
    // unlimited protected prefix.
    return std::make_unique<FCFSPeriodicHybridCircularScheduler>(
        total_nodes, initial_job_count, wait_queue_capacity,
        wait_queue_overflow, backfill_policy, reservation_depth,
        backfill_queue_depth);
  }
  if (backfill_policy == BackfillPolicy::HYBRID) {
    // Hybrid reservations are implemented by the hybrid FCFS schedulers
    // only; route FCFS there and reject other orderings.
    if (reservation_depth == 0) {
      throw std::invalid_argument(
          "hybrid backfilling requires reservation_depth > 0");
    }
    if (priority_policy != PriorityPolicy::FCFS &&
        priority_policy != PriorityPolicy::FCFS_CONSERVATIVE) {
      throw std::invalid_argument(
          "hybrid backfilling is supported with FCFS ordering only");
    }
    return create_hybrid_scheduler(total_nodes, initial_job_count,
                                   backfill_policy, queue_impl,
                                   wait_queue_capacity, wait_queue_overflow,
                                   reservation_depth, backfill_queue_depth);
  }

  // The hybrid classes implement the conservative and hybrid reservation
  // policies only. EASY has its own FCFS implementations, so the
  // FCFS_CONSERVATIVE ordering (FCFS order) with EASY is served by them.
  if (priority_policy == PriorityPolicy::FCFS_CONSERVATIVE &&
      backfill_policy == BackfillPolicy::EASY) {
    priority_policy = PriorityPolicy::FCFS;
  }

  switch (priority_policy) {
  case PriorityPolicy::FCFS:
    // FCFS has 4 queue options
    if (queue_impl == QueueImplementation::BLOCK) {
      // Validate block_size is power of 2
      if (block_size == 0 || (block_size & (block_size - 1)) != 0) {
        std::cerr << "Error: block_size must be a power of 2\n";
        std::exit(1);
      }

      // Compute log2 and use lookup table
      size_t log2_size = 0;
      size_t temp = block_size;
      while (temp > 1) {
        temp >>= 1;
        log2_size++;
      }

      // Verify: (1 << log2_size) == block_size
      if ((1ULL << log2_size) != block_size) {
        std::cerr << "Error: block_size verification failed\n";
        std::exit(1);
      }

      // Dispatch on log2(block_size). This used to be a function-static
      // table of lambdas capturing total_nodes and backfill_policy by
      // reference; the table was built once, so every later call ran the
      // first call's captured references (stack-use-after-return under
      // ASan). A switch has no captured state.
      switch (log2_size) {
      case 2:
        return std::make_unique<FCFSEasyBlockScheduler<4>>(
            total_nodes, backfill_policy, backfill_queue_depth);
      case 3:
        return std::make_unique<FCFSEasyBlockScheduler<8>>(
            total_nodes, backfill_policy, backfill_queue_depth);
      case 4:
        return std::make_unique<FCFSEasyBlockScheduler<16>>(
            total_nodes, backfill_policy, backfill_queue_depth);
      case 5:
        return std::make_unique<FCFSEasyBlockScheduler<32>>(
            total_nodes, backfill_policy, backfill_queue_depth);
      case 6:
        return std::make_unique<FCFSEasyBlockScheduler<64>>(
            total_nodes, backfill_policy, backfill_queue_depth);
      case 7:
        return std::make_unique<FCFSEasyBlockScheduler<128>>(
            total_nodes, backfill_policy, backfill_queue_depth);
      case 8:
        return std::make_unique<FCFSEasyBlockScheduler<256>>(
            total_nodes, backfill_policy, backfill_queue_depth);
      default:
        std::cerr << "Error: block_size " << block_size
                  << " not supported. Use 4, 8, 16, 32, 64, 128, or 256.\n";
        std::exit(1);
      }
    } else if (queue_impl == QueueImplementation::MULTIMAP) {
      return std::make_unique<FCFSEasyMultimapScheduler>(
          total_nodes, backfill_policy, backfill_queue_depth);
    } else if (queue_impl == QueueImplementation::DEQUE) {
      return std::make_unique<FCFSEasyDequeScheduler>(
          total_nodes, backfill_policy, backfill_queue_depth);
    } else if (queue_impl == QueueImplementation::CIRCULAR) {
      return std::make_unique<FCFSEasyCircularScheduler>(
          total_nodes, initial_job_count, backfill_policy, wait_queue_capacity,
          wait_queue_overflow, backfill_queue_depth);
    } else {
      // Defensive: QueueImplementation is a 4-value enum and every
      // value is explicitly handled above - this is only reachable
      // if the enum is extended in the future without updating this
      // factory. Throw rather than silently defaulting to any one
      // implementation, so a missed case is caught immediately
      // instead of silently picking the wrong scheduler.
      throw std::runtime_error(
          "create_scheduler: unhandled QueueImplementation value");
    }

  case PriorityPolicy::FCFS_ALT:
    // FCFS_ALT always uses multimap
    if (queue_impl == QueueImplementation::BLOCK ||
        queue_impl == QueueImplementation::DEQUE) {
      std::cerr << "Warning: queue_impl '" << queue_impl_name(queue_impl)
                << "' not supported for FCFS_ALT, using multimap\n";
    }
    return std::make_unique<FCFSEasyMultimapScheduler>(
        total_nodes, backfill_policy, backfill_queue_depth);

  case PriorityPolicy::FCFS_CONSERVATIVE:
    return create_hybrid_scheduler(total_nodes, initial_job_count,
                                   backfill_policy, queue_impl,
                                   wait_queue_capacity, wait_queue_overflow,
                                   reservation_depth, backfill_queue_depth);

  case PriorityPolicy::SJF:
    // SJF only uses multimap (already efficient)
    if (queue_impl != QueueImplementation::CIRCULAR) {
      std::cerr << "Warning: queue_impl '" << queue_impl_name(queue_impl)
                << "' not supported for SJF, using default multimap\n";
    }
    return std::make_unique<SJFEasyScheduler>(total_nodes, backfill_policy,
                                              backfill_queue_depth);

  case PriorityPolicy::LJF:
    // LJF only uses multimap (already efficient)
    if (queue_impl != QueueImplementation::CIRCULAR) {
      std::cerr << "Warning: queue_impl '" << queue_impl_name(queue_impl)
                << "' not supported for LJF, using default multimap\n";
    }
    return std::make_unique<LJFEasyScheduler>(total_nodes, backfill_policy,
                                              backfill_queue_depth);

  default:
    // Default to FCFS with deque
    return std::make_unique<FCFSEasyDequeScheduler>(
        total_nodes, backfill_policy, backfill_queue_depth);
  }
}

} // namespace dr_evt
