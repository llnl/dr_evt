/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

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
