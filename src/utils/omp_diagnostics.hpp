/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_UTILS_OMP_DIAGNOSTICS_HPP
#define DR_EVT_UTILS_OMP_DIAGNOSTICS_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

/**
 *  Obtain the processor affinity of the current thread.
 *  This structure needs to be thread local.
 */
struct my_omp_affinity {
  using cpuid_t = uint8_t;     ///< Compact type used for processing-unit IDs.
  int m_tid;                   ///< Calling thread's OpenMP thread identifier.
  int m_num_threads;           ///< Number of threads in the current team.
  int m_my_level;              ///< Nesting level of the current OpenMP region.
  std::vector<cpuid_t> m_cpus; ///< Processing units available to this thread.
  std::vector<int> m_ancestor_id; ///< Ancestor thread IDs by nesting level.

  /**
   * @brief Gather the calling thread's OpenMP identity and CPU affinity.
   *
   * When this is called inside of a parallel region, it will gather
   * information on the processor affinity of the calling thread.
   */
  void get();
  /**
   * @brief Print the affinity information previously gathered by get().
   *
   * Print out the processor affinity of the thread gathered by `get()`.
   * This call does not need to be inside of a parallel region as it
   * simply prints out the information gathered.
   */
  void print() const;
};

/** @brief Return the OpenMP runtime version string.
 * @return `_OPENMP` and its corresponding specification version, or `"NA"`
 * when OpenMP support is unavailable. */
std::string get_omp_version();
/**
 * @brief Convert an OpenMP scheduling kind into a readable name.
 *
 * @param[in] kind Integer representation of an OpenMP `omp_sched_t` value,
 * typically obtained through `omp_get_schedule()`; supported values describe
 * the `static`, `dynamic`, `guided`, and `auto` runtime scheduling policies.
 * @return The symbolic OpenMP schedule name, or `"unknown"` when @p kind is
 * not recognized or OpenMP support is unavailable.
 */
std::string to_string_omp_schedule_kind(int kind);
/** @brief Configure the OpenMP runtime to use static scheduling. */
void set_static_schedule();

/**@}*/
} // namespace dr_evt
#endif // DR_EVT_UTILS_OMP_DIAGNOSTICS_HPP
