/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file common.hpp
 * @brief Project-wide compiler, feature, and trace-processing macros.
 */

#ifndef DR_EVT_COMMON_HPP
#define DR_EVT_COMMON_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

/**
 * @brief Compiler-specific restrict qualifier for pointer declarations.
 * @details Expands to __restrict__ on GCC, Clang (including Apple Clang), and
 * Intel compilers, and nothing on other compilers. Use only where accessed
 * storage satisfies the compiler's restrict aliasing contract
 * throughout the pointer's scope; violating that contract can miscompile code.
 * This macro provides syntax portability, not an aliasing check. Restrict is
 * not standard C++; restrict-qualified references and member functions are
 * outside this macro's portability contract.
 */
#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER) ||    \
    defined(__INTEL_LLVM_COMPILER)
#define DR_EVT_RESTRICT __restrict__
#else
#define DR_EVT_RESTRICT
#endif

/**
 *  This will enable to print out only time_limit vs exec_time.
 */
// #define LIMIT_VS_EXEC_TIME_ONLY 1

// The dedicated allocation time (DAT) requires an application in advance
// to gain execlusive access to the entire compute resources of a cluster.

/// Include DAT jobs that ran through the pAll queue.
#define INCLUDE_DAT 1

/// Track dedicated-allocation-time periods in trace processing.
#define MARK_DAT_PERIOD 1

// Includes jobs from all the queues rather than pbatch and pall
// #define SHOW_ALL_QUEUE 1

/// Group all pbatch[0-3] queues into the canonical pBatch queue.
#define PBATCH_GROUP 1

/**
 *  Show the record number in the raw file for each record.
 *  The record number starts from 1 and increase by the line.
 */
// #define SHOW_ORG_NO 1

/**
 *  The limit on the amount of node resource a batch job can request on Lassen.
 *  Comment out to avoid applying this filtering. Jobs with such an error will
 *  be ignored.
 */
// #define BATCH_JOB_NODE_LIMIT 256

/**
 * Make sure that job submit time is earlier than the job begin time and that
 * job begin time is earler than job end time. Jobs with such an error will be
 * ignored.
 */
/// Reject trace records whose submit, begin, and end times are not ordered.
#define EVENT_TIME_ORDER 1

/**
 *  For PST timezone. This information is needed to handle daylight saving time.
 *  If this is not defined, UTC is assumed.
 */
/// Default timezone for trace timestamps that do not contain an offset.
#define DATA_TIMEZONE "PST8PDT"

#if !defined(DATA_TIMEZONE)
#define DATA_TIMEZONE "" // Assume UTC
#endif

#if LIMIT_VS_EXEC_TIME_ONLY
#ifdef INCLUDE_DAT
#undef INCLUDE_DAT
#endif
#endif

/** @brief Compare two primary keys and use a tie-break expression on equality.
 * @param[in] _A Left primary key expression.
 * @param[in] _B Right primary key expression.
 * @param[in] _T Boolean tie-break expression evaluated when keys are equal. */
#define LESS_OR(_A, _B, _T) (((_A) < (_B)) || (((_A) == (_B)) && (_T)))

namespace dr_evt {
/** \addtogroup dr_evt_global
 *  @{ */

/**@}*/
} // end of namespace dr_evt

#include "dr_evt_types.hpp"

#endif // DR_EVT_COMMON_HPP
