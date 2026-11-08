/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file block_wait_queue.cpp
 * @brief Explicit instantiations of supported block waiting queues.
 * @details Builds the supported block sizes for the FCFS EASY queue. This
 * file adds no scheduling rule.
 * @note Used by --priority_policy fcfs --queue_impl block --backfill_policy
 * easy, with both intervals zero. --block_size selects the instantiated
 * size. Protobuf uses queue_impl: "block" and block_size.
 */

#include "sim/block_wait_queue.hpp"

namespace dr_evt {

// Explicit template instantiations for common block sizes
template class BlockWaitQueue<4>;
template class BlockWaitQueue<8>;
template class BlockWaitQueue<16>;
template class BlockWaitQueue<32>;
template class BlockWaitQueue<64>;
template class BlockWaitQueue<128>;
template class BlockWaitQueue<256>;

} // namespace dr_evt
