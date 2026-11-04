/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file multi_system_backfill_scheduler.cpp
 * @brief Implementation of multi-system FCFS with spatial backfilling.
 */

#include "multi_system_backfill_scheduler.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace dr_evt {
namespace experimental {

namespace {

/** @brief Strip surrounding whitespace.
 * @param[in] text Input text.
 * @return Trimmed copy. */
std::string trim(const std::string &text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return std::string();
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

/** @brief Parse a positive node count.
 * @param[in] text Decimal text.
 * @param[in] context Description used in the error message.
 * @return Parsed count.
 * @throws std::invalid_argument when the text is not a positive integer. */
num_nodes_t parse_capacity(const std::string &text,
                           const std::string &context) {
  size_t consumed = 0;
  unsigned long value = 0;
  try {
    value = std::stoul(text, &consumed);
  } catch (const std::exception &) {
    throw std::invalid_argument("invalid node count '" + text + "' in " +
                                context);
  }
  if (consumed != text.size() || value == 0 ||
      value > std::numeric_limits<num_nodes_t>::max()) {
    throw std::invalid_argument("invalid node count '" + text + "' in " +
                                context);
  }
  return static_cast<num_nodes_t>(value);
}

} // anonymous namespace

SpatialBackfillPolicy parse_spatial_backfill_policy(const std::string &name) {
  if (name == "target_only") {
    return SpatialBackfillPolicy::TARGET_ONLY;
  }
  if (name == "every_system") {
    return SpatialBackfillPolicy::EVERY_SYSTEM;
  }
  if (name == "global_shadow") {
    return SpatialBackfillPolicy::GLOBAL_SHADOW;
  }
  throw std::invalid_argument("unknown spatial backfill policy '" + name +
                              "' (expected target_only, every_system, or "
                              "global_shadow)");
}

PlacementPolicy parse_placement_policy(const std::string &name) {
  if (name == "best_fit") {
    return PlacementPolicy::BEST_FIT;
  }
  if (name == "first_fit") {
    return PlacementPolicy::FIRST_FIT;
  }
  if (name == "worst_fit") {
    return PlacementPolicy::WORST_FIT;
  }
  throw std::invalid_argument("unknown placement policy '" + name +
                              "' (expected best_fit, first_fit, or worst_fit)");
}

const char *to_string(SpatialBackfillPolicy policy) {
  switch (policy) {
  case SpatialBackfillPolicy::TARGET_ONLY:
    return "target_only";
  case SpatialBackfillPolicy::EVERY_SYSTEM:
    return "every_system";
  case SpatialBackfillPolicy::GLOBAL_SHADOW:
    return "global_shadow";
  }
  return "unknown";
}

const char *to_string(PlacementPolicy policy) {
  switch (policy) {
  case PlacementPolicy::BEST_FIT:
    return "best_fit";
  case PlacementPolicy::FIRST_FIT:
    return "first_fit";
  case PlacementPolicy::WORST_FIT:
    return "worst_fit";
  }
  return "unknown";
}

std::vector<System_Spec> load_systems_file(const std::string &filename) {
  std::ifstream input(filename);
  if (!input) {
    throw std::runtime_error("cannot open systems file '" + filename + "'");
  }
  std::vector<System_Spec> systems;
  std::string line;
  size_t line_no = 0;
  while (std::getline(input, line)) {
    ++line_no;
    const std::string text = trim(line);
    if (text.empty() || text[0] == '#') {
      continue;
    }
    std::stringstream fields(text);
    std::string name;
    std::string size;
    if (!std::getline(fields, name, ',') || !std::getline(fields, size, ',')) {
      throw std::runtime_error("malformed systems line " +
                               std::to_string(line_no) + " in '" + filename +
                               "'");
    }
    systems.push_back(
        {trim(name), parse_capacity(trim(size), "systems file line " +
                                                    std::to_string(line_no))});
  }
  return systems;
}

std::vector<System_Spec> parse_systems_inline(const std::string &text) {
  std::vector<System_Spec> systems;
  std::stringstream items(text);
  std::string item;
  while (std::getline(items, item, ',')) {
    const std::string spec = trim(item);
    if (spec.empty()) {
      continue;
    }
    const auto colon = spec.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == spec.size()) {
      throw std::invalid_argument("system '" + spec +
                                  "' needs the form name:nodes");
    }
    systems.push_back({trim(spec.substr(0, colon)),
                       parse_capacity(trim(spec.substr(colon + 1)),
                                      "--systems entry '" + spec + "'")});
  }
  return systems;
}

void validate_systems(const std::vector<System_Spec> &systems) {
  if (systems.empty()) {
    throw std::invalid_argument("at least one system is required");
  }
  std::set<std::string> names;
  for (const auto &system : systems) {
    if (system.name.empty()) {
      throw std::invalid_argument("system names must not be empty");
    }
    if (!names.insert(system.name).second) {
      throw std::invalid_argument("duplicate system name '" + system.name +
                                  "'");
    }
    if (system.capacity == 0) {
      throw std::invalid_argument("system '" + system.name +
                                  "' must have a positive capacity");
    }
  }
}

// ---------------------------------------------------------------------------
// CountResourceModel
// ---------------------------------------------------------------------------

CountResourceModel::CountResourceModel(std::vector<System_Spec> systems)
    : m_systems(std::move(systems)), m_used(m_systems.size(), 0u),
      m_max_capacity(0u), m_total_capacity(0u) {
  validate_systems(m_systems);
  for (const auto &system : m_systems) {
    m_max_capacity = std::max(m_max_capacity, system.capacity);
    m_total_capacity += system.capacity;
  }
}

sim_time_t CountResourceModel::earliest_fit(system_id_t k,
                                            num_nodes_t nodes_requested,
                                            std::vector<release_t> &releases,
                                            sim_time_t now) const {
  if (!admissible(k, nodes_requested)) {
    return std::numeric_limits<sim_time_t>::infinity();
  }
  const num_nodes_t need = demand(k, nodes_requested);
  num_nodes_t available = free(k);
  if (need <= available) {
    return now;
  }
  std::sort(releases.begin(), releases.end());
  for (const auto &[end_time, freed] : releases) {
    available += freed;
    if (available >= need) {
      return end_time;
    }
  }
  return std::numeric_limits<sim_time_t>::infinity();
}

void CountResourceModel::reset_usage() {
  std::fill(m_used.begin(), m_used.end(), 0u);
}

num_nodes_t CountResourceModel::total_used() const {
  num_nodes_t total = 0;
  for (const auto used : m_used) {
    total += used;
  }
  return total;
}

// ---------------------------------------------------------------------------
// MultiSystemBackfillScheduler
// ---------------------------------------------------------------------------

MultiSystemBackfillScheduler::MultiSystemBackfillScheduler(
    std::vector<System_Spec> systems, SpatialBackfillPolicy spatial_policy,
    PlacementPolicy placement_policy, BackfillPolicy bf_policy,
    size_t initial_job_count)
    : FCFSEasyCustomScheduler(CountResourceModel(systems).total_capacity(),
                              initial_job_count, bf_policy,
                              /*num_max_candidates=*/1, /*initial_capacity=*/1,
                              CircularOverflowPolicy::GROW),
      m_eligible_end_idx(0), m_current_tracked_time(0.0), m_removed_count(0),
      m_model(std::move(systems)), m_spatial_policy(spatial_policy),
      m_placement_policy(placement_policy),
      m_shadow_times(m_model.num_systems(),
                     std::numeric_limits<sim_time_t>::infinity()),
      m_reserved_system(static_cast<system_id_t>(m_model.num_systems())),
      m_jobs_started_by_system(m_model.num_systems(), 0u) {
  if (bf_policy == BackfillPolicy::CONSERVATIVE) {
    throw std::invalid_argument(
        "MultiSystemBackfillScheduler supports EASY (spatial backfilling) or "
        "NONE; temporal conservative backfilling is not implemented");
  }
}

void MultiSystemBackfillScheduler::insert_job(job_no_t job_id,
                                              sim_time_t submit_time,
                                              tdiff_t run_time_estimate,
                                              num_nodes_t nodes_requested) {
  if (!can_ever_run(nodes_requested)) {
    throw std::logic_error("job " + std::to_string(job_id) + " requests " +
                           std::to_string(nodes_requested) +
                           " nodes, more than the largest system (" +
                           std::to_string(m_model.max_capacity()) +
                           "); the caller must consult can_ever_run() first");
  }
  m_wait_queue.emplace_back(job_id, submit_time, run_time_estimate,
                            nodes_requested);
  if (submit_time <= m_current_tracked_time) {
    m_eligible_end_idx = m_wait_queue.size();
  }
}

void MultiSystemBackfillScheduler::sync_to(sim_time_t current_time) {
  if (current_time <= m_current_tracked_time) {
    return;
  }
  while (m_eligible_end_idx < m_wait_queue.size() &&
         m_wait_queue[m_eligible_end_idx].submit_time <= current_time) {
    ++m_eligible_end_idx;
  }
  m_current_tracked_time = current_time;
}

sim_time_t MultiSystemBackfillScheduler::get_next_arrival_time() {
  for (size_t i = m_eligible_end_idx; i < m_wait_queue.size(); ++i) {
    if (!m_wait_queue[i].removed) {
      return m_wait_queue[i].submit_time;
    }
  }
  return std::numeric_limits<sim_time_t>::max();
}

std::vector<job_no_t> MultiSystemBackfillScheduler::pending_job_ids() const {
  std::vector<job_no_t> result;
  for (const auto &job : m_wait_queue) {
    if (!job.removed) {
      result.push_back(job.job_id);
    }
  }
  return result;
}

std::optional<tdiff_t>
MultiSystemBackfillScheduler::waiting_resource_area() const {
  tdiff_t area = 0.0;
  for (size_t i = 0; i < m_eligible_end_idx; ++i) {
    const auto &job = m_wait_queue[i];
    if (!job.removed) {
      area += static_cast<tdiff_t>(job.nodes_requested) * job.run_time_estimate;
    }
  }
  return area;
}

void MultiSystemBackfillScheduler::compact_if_needed() {
  if (m_removed_count == 0 || m_removed_count * 2 <= m_wait_queue.size()) {
    return;
  }
  size_t eligible_removed = 0;
  for (size_t idx = 0; idx < m_eligible_end_idx && idx < m_wait_queue.size();
       ++idx) {
    if (m_wait_queue[idx].removed) {
      ++eligible_removed;
    }
  }
  auto new_end =
      std::remove_if(m_wait_queue.begin(), m_wait_queue.end(),
                     [](const JobEntry &entry) { return entry.removed; });
  m_wait_queue.erase(new_end, m_wait_queue.end());
  m_eligible_end_idx -= eligible_removed;
  m_removed_count = 0;
}

system_id_t MultiSystemBackfillScheduler::choose_system(
    num_nodes_t nodes_requested,
    const std::vector<system_id_t> &candidates) const {
  system_id_t best = candidates.front();
  if (m_placement_policy == PlacementPolicy::FIRST_FIT) {
    return best;
  }
  num_nodes_t best_left = m_model.leftover(best, nodes_requested);
  for (size_t i = 1; i < candidates.size(); ++i) {
    const system_id_t k = candidates[i];
    const num_nodes_t left = m_model.leftover(k, nodes_requested);
    const bool better = m_placement_policy == PlacementPolicy::BEST_FIT
                            ? left < best_left
                            : left > best_left;
    if (better) {
      best = k;
      best_left = left;
    }
  }
  return best;
}

void MultiSystemBackfillScheduler::place(
    const JobEntry &job, system_id_t k, sim_time_t current_time,
    std::vector<std::vector<CountResourceModel::release_t>> &releases) {
  const num_nodes_t demand = m_model.demand(k, job.nodes_requested);
  m_model.allocate(k, demand);
  m_placements[job.job_id] = {k, demand};
  releases[k].push_back({current_time + job.run_time_estimate, demand});
  ++m_jobs_started_by_system[k];
}

std::vector<job_no_t>
MultiSystemBackfillScheduler::schedule(num_nodes_t free_nodes,
                                       const running_jobs_t &running_jobs,
                                       sim_time_t current_time) {
  sync_to(current_time);
  compact_if_needed();
  if (m_eligible_end_idx == 0) {
    return {};
  }

  const size_t num_systems = m_model.num_systems();

  // Rebuild per-system usage and projected releases from the caller's
  // running-job map and this scheduler's own placement record.
  m_model.reset_usage();
  std::vector<std::vector<CountResourceModel::release_t>> releases(num_systems);
  for (const auto &[job_id, running] : running_jobs) {
    const auto placed = m_placements.find(job_id);
    if (placed == m_placements.end()) {
      throw std::logic_error("running job " + std::to_string(job_id) +
                             " has no recorded placement; this scheduler "
                             "requires simulation mode without warm start");
    }
    const Placement &placement = placed->second;
    m_model.allocate(placement.system, placement.demand);
    const sim_time_t end_time = running.start_time + running.run_time;
    if (end_time > current_time) {
      releases[placement.system].push_back({end_time, placement.demand});
    }
  }
  if (m_model.total_used() + free_nodes != m_model.total_capacity()) {
    throw std::logic_error("per-system usage (" +
                           std::to_string(m_model.total_used()) +
                           ") plus free nodes (" + std::to_string(free_nodes) +
                           ") does not equal the total capacity (" +
                           std::to_string(m_model.total_capacity()) + ")");
  }

  std::vector<job_no_t> jobs_to_run;
  std::vector<system_id_t> candidates;
  candidates.reserve(num_systems);

  // Step 1: start heads while one fits somewhere. A removed entry at the
  // front is a stale placeholder from an earlier backfill.
  while (m_eligible_end_idx > 0 && !m_wait_queue.empty()) {
    const JobEntry &head = m_wait_queue.front();
    if (head.removed) {
      --m_removed_count;
      m_wait_queue.pop_front();
      --m_eligible_end_idx;
      continue;
    }
    candidates.clear();
    for (system_id_t k = 0; k < num_systems; ++k) {
      if (m_model.fits_now(k, head.nodes_requested)) {
        candidates.push_back(k);
      }
    }
    if (candidates.empty()) {
      break;
    }
    const system_id_t k = choose_system(head.nodes_requested, candidates);
    place(head, k, current_time, releases);
    jobs_to_run.push_back(head.job_id);
    m_wait_queue.pop_front();
    --m_eligible_end_idx;
  }

  if (active_job_count() == 0) {
    m_reserved_system = static_cast<system_id_t>(num_systems);
    std::fill(m_shadow_times.begin(), m_shadow_times.end(),
              std::numeric_limits<sim_time_t>::infinity());
    return jobs_to_run;
  }

  // Step 2: the head is blocked on every system. Project it per system.
  const JobEntry &head = m_wait_queue.front();
  sim_time_t t_star = std::numeric_limits<sim_time_t>::infinity();
  system_id_t k_star = static_cast<system_id_t>(num_systems);
  for (system_id_t k = 0; k < num_systems; ++k) {
    m_shadow_times[k] = m_model.earliest_fit(k, head.nodes_requested,
                                             releases[k], current_time);
    if (m_shadow_times[k] < t_star) {
      t_star = m_shadow_times[k];
      k_star = k;
    }
  }
  m_fcfs_reservation_time = t_star;
  m_reserved_system = k_star;

  if (m_backfill_policy == BackfillPolicy::NONE) {
    return jobs_to_run;
  }

  std::vector<sim_time_t> windows(num_systems);
  for (system_id_t k = 0; k < num_systems; ++k) {
    switch (m_spatial_policy) {
    case SpatialBackfillPolicy::TARGET_ONLY:
      windows[k] =
          (k == k_star) ? t_star : std::numeric_limits<sim_time_t>::infinity();
      break;
    case SpatialBackfillPolicy::EVERY_SYSTEM:
      windows[k] = m_shadow_times[k];
      break;
    case SpatialBackfillPolicy::GLOBAL_SHADOW:
      windows[k] = std::isinf(m_shadow_times[k])
                       ? std::numeric_limits<sim_time_t>::infinity()
                       : t_star;
      break;
    }
  }

  std::vector<Reservation_Timeline> plans;
  plans.reserve(num_systems);
  for (system_id_t k = 0; k < num_systems; ++k) {
    std::map<sim_time_t, num_nodes_t> profile;
    profile[current_time] = m_model.free(k);
    std::map<sim_time_t, num_nodes_t> releases_at;
    for (const auto &[time, nodes] : releases[k]) {
      if (time > current_time) {
        releases_at[time] += nodes;
      }
    }
    num_nodes_t available = m_model.free(k);
    for (const auto &[time, nodes] : releases_at) {
      available += nodes;
      profile[time] = available;
    }
    plans.emplace_back(std::move(profile));
    if (std::isfinite(windows[k]) && plans.back().can_fit(
            windows[k], head.nodes_requested, head.run_time_estimate)) {
      plans.back().reserve(windows[k], head.nodes_requested,
                           head.run_time_estimate);
    }
  }

  for (size_t i = 1; i < m_eligible_end_idx; ++i) {
    const JobEntry &job = m_wait_queue[i];
    if (job.removed) {
      continue;
    }
    candidates.clear();
    for (system_id_t k = 0; k < num_systems; ++k) {
      if (m_model.fits_now(k, job.nodes_requested) &&
          (!std::isfinite(windows[k]) || plans[k].can_fit(
              current_time, job.nodes_requested, job.run_time_estimate))) {
        candidates.push_back(k);
      }
    }
    if (candidates.empty()) {
      continue;
    }
    const system_id_t k = choose_system(job.nodes_requested, candidates);
    place(job, k, current_time, releases);
    plans[k].reserve(current_time, job.nodes_requested,
                     job.run_time_estimate);
    jobs_to_run.push_back(job.job_id);
    m_wait_queue[i].removed = true;
    ++m_removed_count;
  }

  return jobs_to_run;
}

} // namespace experimental
} // namespace dr_evt
