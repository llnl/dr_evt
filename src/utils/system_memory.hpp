/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef  DR_EVT_UTILS_SYSTEM_MEMORY_HPP
#define  DR_EVT_UTILS_SYSTEM_MEMORY_HPP
#include <cstddef>

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

/**
 * @brief Return memory currently available for new allocations without swap.
 * @details On Linux, reads `/proc/meminfo`'s `MemAvailable` value, which
 * includes reclaimable cache and buffer pages rather than only free pages.
 * Other platforms are not implemented yet. Callers performing a
 * memory-pressure check must treat zero as "unknown; do not enforce", not as
 * "no memory available". Zero is returned on non-Linux hosts and when the
 * Linux value cannot be read or is unavailable (for example, pre-3.14
 * kernels).
 *
 * For deterministic tests, the `DR_EVT_TEST_AVAILABLE_MEMORY_BYTES`
 * environment variable may provide a non-negative integer byte count. A
 * malformed value is ignored and the platform query proceeds normally. This
 * environment variable is a test seam, not a production configuration API.
 * @return Available-memory estimate in bytes as std::size_t, or zero when
 *         the estimate is unknown.
 */
std::size_t get_available_memory_bytes();

/**@}*/
} // end of namespace dr_evt
#endif //  DR_EVT_UTILS_SYSTEM_MEMORY_HPP
