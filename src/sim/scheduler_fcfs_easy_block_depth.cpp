/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_fcfs_easy_block_depth.cpp
 * @brief Block-queue FCFS backfill under --backfill_queue_depth.
 * @details Kept in its own translation unit: compiled together with
 * schedule(), this code consumed the compiler's per-file inlining budget and
 * the default block search lost about 10 percent of its speed.
 */

#include "sim/scheduler_fcfs_easy_block.hpp"
#include <stdexcept>

namespace dr_evt {

// A queue depth needs every examined job counted in FCFS order, which the
// block search's per-block pre-filters skip over, so the arrival order is
// scanned directly, as the deque scheduler does.
template <size_t BlockSize>
std::vector<job_no_t>
FCFSEasyBlockScheduler<BlockSize>::backfill_with_queue_depth(
    bool head_planned, sim_time_t planned_reservation, sim_time_t current_time,
    Reservation_Timeline reservation_plan,
    num_nodes_t available_nodes, std::vector<job_no_t> jobs_to_run) {
  size_t head_index = 0;
  while (head_index < m_eligible_end_idx && m_job_order[head_index].removed) {
    ++head_index;
  }
  const size_t scan_end =
      queue_depth_scan_end(m_job_order, head_index + 1, m_eligible_end_idx,
                           m_backfill_queue_depth, head_planned);
  for (size_t i = head_index + 1; i < scan_end && available_nodes > 0; ++i) {
    if (m_job_order[i].removed) {
      continue;
    }
    const job_no_t job_id = m_job_order[i].job_id;
    tdiff_t run_time = 0.0;
    num_nodes_t nodes = 0;
    if (!m_wait_queue.get_job_info(job_id, run_time, nodes)) {
      throw std::logic_error("active block-queue job is missing");
    }
    if (nodes > available_nodes) {
      continue;
    }
#ifdef DR_EVT_LEGACY_RESERVATION_BARRIER
    if (current_time + run_time >= planned_reservation)
      continue;
#else
    if (!reservation_plan.can_fit(current_time, nodes,
                                  release_estimate(current_time, run_time)))
      continue;
#endif
    jobs_to_run.push_back(job_id);
    available_nodes -= nodes;
    reservation_plan.reserve(current_time, nodes,
                             release_estimate(current_time, run_time));
    m_wait_queue.remove(job_id);
    m_job_order[i].removed = true;
    ++m_removed_count;
  }
  return jobs_to_run;
}

// Explicit instantiations for the supported block sizes.
template std::vector<job_no_t>
FCFSEasyBlockScheduler<4>::backfill_with_queue_depth(bool, sim_time_t,
                                                     sim_time_t, Reservation_Timeline, num_nodes_t,
                                                     std::vector<job_no_t>);
template std::vector<job_no_t>
FCFSEasyBlockScheduler<8>::backfill_with_queue_depth(bool, sim_time_t,
                                                     sim_time_t, Reservation_Timeline, num_nodes_t,
                                                     std::vector<job_no_t>);
template std::vector<job_no_t>
FCFSEasyBlockScheduler<16>::backfill_with_queue_depth(bool, sim_time_t,
                                                      sim_time_t, Reservation_Timeline, num_nodes_t,
                                                      std::vector<job_no_t>);
template std::vector<job_no_t>
FCFSEasyBlockScheduler<32>::backfill_with_queue_depth(bool, sim_time_t,
                                                      sim_time_t, Reservation_Timeline, num_nodes_t,
                                                      std::vector<job_no_t>);
template std::vector<job_no_t>
FCFSEasyBlockScheduler<64>::backfill_with_queue_depth(bool, sim_time_t,
                                                      sim_time_t, Reservation_Timeline, num_nodes_t,
                                                      std::vector<job_no_t>);
template std::vector<job_no_t>
FCFSEasyBlockScheduler<128>::backfill_with_queue_depth(bool, sim_time_t,
                                                       sim_time_t, Reservation_Timeline, num_nodes_t,
                                                       std::vector<job_no_t>);
template std::vector<job_no_t>
FCFSEasyBlockScheduler<256>::backfill_with_queue_depth(bool, sim_time_t,
                                                       sim_time_t, Reservation_Timeline, num_nodes_t,
                                                       std::vector<job_no_t>);

} // namespace dr_evt
