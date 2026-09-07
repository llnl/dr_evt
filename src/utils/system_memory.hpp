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

/// Returns the amount of memory (in bytes) currently available to new
/// allocations without swapping, or 0 if it can't be determined on this
/// platform. Backed by /proc/meminfo's `MemAvailable` on Linux (the
/// kernel's own estimate, accounting for reclaimable caches/buffers -
/// not just free pages, which understates what's actually available);
/// no equivalent is implemented for other platforms yet, so this always
/// returns 0 there today.
///
/// Callers that use this for a memory-pressure check should treat 0 as
/// "unknown - nothing to enforce against", not as "no memory available":
/// this happens on any non-Linux platform, or on Linux if /proc/meminfo
/// is unreadable or lacks a MemAvailable line (kernels older than 3.14).
///
/// Test seam: if the environment variable
/// DR_EVT_TEST_AVAILABLE_MEMORY_BYTES is set to a valid non-negative
/// integer, its value is returned directly instead of querying the
/// system - lets tests exercise memory-pressure behavior deterministically
/// without needing an actual low-memory machine. Not intended for
/// production use.
std::size_t get_available_memory_bytes();

/**@}*/
} // end of namespace dr_evt
#endif //  DR_EVT_UTILS_SYSTEM_MEMORY_HPP
