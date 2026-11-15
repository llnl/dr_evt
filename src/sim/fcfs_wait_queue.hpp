/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file fcfs_wait_queue.hpp
 * @brief Wait-queue storage choices shared by the FCFS schedulers.
 * @details The FCFS schedulers are templates over one of two storage
 * policies, so every scheduling feature is available with either container
 * and the two differ only in how the waiting jobs are stored. Both expose the
 * subset of the standard sequence interface the schedulers use: indexing,
 * front(), pop_front(), push_back(), size(), begin()/end(), and erase().
 * @note CLI: --queue_impl circular selects CircularWaitQueue (default);
 * --queue_impl deque selects DequeWaitQueue. --wait_queue_capacity and
 * --wait_queue_overflow apply to the circular queue only.
 * Protobuf: Simulation_Params and InitRequest use the same field names
 * without the leading --; see dr_evt_params.proto and dr_evt_service.proto.
 */

#ifndef DR_EVT_SIM_FCFS_WAIT_QUEUE_HPP
#define DR_EVT_SIM_FCFS_WAIT_QUEUE_HPP

#include "params/sim_params.hpp"
#include <algorithm>
#include <boost/circular_buffer.hpp>
#include <deque>
#include <stdexcept>
#include <string>
#include <utility>

namespace dr_evt {

/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * @brief Contiguous ring-buffer storage for waiting jobs.
 * @details boost::circular_buffer keeps its elements in one allocation, so the
 * repeated indexed scans of the backfill passes stay cache friendly. Its
 * capacity is fixed unless the configured overflow policy permits growth;
 * growth doubles the capacity and copies the existing entries.
 */
struct CircularWaitQueue {
  /// Queue container for entries of type Entry.
  template <typename Entry> using Storage = boost::circular_buffer<Entry>;
  /// Name used in scheduler class names and diagnostics.
  static constexpr const char *label = "Circular";

  /**
   * @brief Create an empty queue.
   * @tparam Entry Queue entry type.
   * @param[in] capacity Initial capacity in entries.
   * @return Empty queue holding capacity entries before it must grow.
   */
  template <typename Entry> static Storage<Entry> create(size_t capacity) {
    return Storage<Entry>(capacity);
  }

  /**
   * @brief Append an entry, enlarging a full queue when the policy allows.
   * @tparam Entry Queue entry type.
   * @param[in,out] queue Destination queue.
   * @param[in] entry Entry to append.
   * @param[in] policy Action when the queue is full.
   * @param[in] owner Scheduler class name for the capacity error message.
   * @throws std::runtime_error if the queue is full and policy is ABORT.
   * @throws std::bad_alloc if growing the queue fails.
   */
  template <typename Entry>
  static void push_back(Storage<Entry> &queue, Entry &&entry,
                        CircularOverflowPolicy policy,
                        const std::string &owner) {
    if (queue.full()) {
      if (policy == CircularOverflowPolicy::ABORT) {
        throw std::runtime_error(
            owner + ": wait queue capacity (" +
            std::to_string(queue.capacity()) +
            ") exceeded; use --wait_queue_overflow grow or a larger "
            "--wait_queue_capacity");
      }
      // GROW doubles the capacity, matching std::vector's amortized growth.
      // set_capacity() keeps every existing entry because it never shrinks.
      queue.set_capacity(std::max<size_t>(queue.capacity() * 2, 1));
    }
    queue.push_back(std::move(entry));
  }
};

/**
 * @brief Chunked storage for waiting jobs that grows without bound.
 * @details std::deque allocates in fixed-size chunks, so it never copies
 * existing entries on growth and has no capacity limit. The initial capacity
 * and overflow policy have no meaning here and are ignored.
 */
struct DequeWaitQueue {
  /// Queue container for entries of type Entry.
  template <typename Entry> using Storage = std::deque<Entry>;
  /// Name used in scheduler class names and diagnostics.
  static constexpr const char *label = "Deque";

  /**
   * @brief Create an empty queue.
   * @tparam Entry Queue entry type.
   * @return Empty queue; the requested capacity is ignored.
   */
  template <typename Entry> static Storage<Entry> create(size_t) { return {}; }

  /**
   * @brief Append an entry.
   * @tparam Entry Queue entry type.
   * @param[in,out] queue Destination queue.
   * @param[in] entry Entry to append.
   * @throws std::bad_alloc if storage cannot be allocated.
   */
  template <typename Entry>
  static void push_back(Storage<Entry> &queue, Entry &&entry,
                        CircularOverflowPolicy, const std::string &) {
    queue.push_back(std::move(entry));
  }
};

/**@}*/

} // namespace dr_evt

#endif // DR_EVT_SIM_FCFS_WAIT_QUEUE_HPP
