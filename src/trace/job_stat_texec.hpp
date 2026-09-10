/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file job_stat_texec.hpp
 * @brief Execution-time statistics across job records.
 */

#ifndef DR_EVT_TRACE_JOB_STAT_TEXEC_HPP
#define DR_EVT_TRACE_JOB_STAT_TEXEC_HPP
#include "common.hpp"
#include "trace/job_record.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 * @brief Accumulate actual-runtime distributions grouped by requested limit.
 * @tparam N Number of equal timeout-fraction bins.
 */
template <size_t N> class Job_Stat_Texec {
public:
  /**
   *  Execution time distribution. If the number of bins is 4 for instance,
   *  each bin represents a quartile of the timeout--[0%, 25%), [25%, 50%),
   *  [50%, 75%), and [75%, 100%)--to which the execution time of each job
   *  falls into. The value of a bin represents the number of jobs belong to
   *  the quartile, and the distribution shows how long jobs actually ran
   *  compared to the timeout set by users.
   */
  using tbins_t = typename std::array<num_jobs_t, N>;

  /**
   *  Execution time distribution for each resource set, i.e., the number of
   *  nodes.
   */
  using tbins_by_nnodes_t = typename std::map<num_nodes_t, tbins_t>;

  struct timeout_slot {
    /// Jobs accumulated for this requested-time-limit slot.
    num_jobs_t m_num_jobs;
    /// Per-node-count runtime-fraction histograms.
    tbins_by_nnodes_t m_bins;

    /** @brief Construct an empty requested-time-limit slot. */
    timeout_slot() : m_num_jobs(static_cast<num_jobs_t>(0u)) {};
  };

  /** @brief Runtime histograms indexed by requested job time limit. */
  using tjob_t = std::map<timeout_t, timeout_slot>;

protected:
  /// Histograms indexed by requested job time limit.
  tjob_t m_tjob;

public:
  /** @brief Construct an empty runtime-statistics accumulator.
   * @throws std::invalid_argument when N is zero. */
  Job_Stat_Texec();

  /** @brief Add one job's actual-runtime observation.
   * @param[in] j Job record supplying limit, actual runtime, and node count. */
  void add_stat(const Job_Record &j);
};

template <std::size_t N> Job_Stat_Texec<N>::Job_Stat_Texec() {
  if (N == 0ul) {
    std::string err = "Number of limit time segments should not be zero.";
    throw std::invalid_argument(err);
  }
}

template <std::size_t N>
void Job_Stat_Texec<N>::add_stat(const Job_Records &j) {
  const auto t_exec = j.get_actual_run_time();
  const auto t_limit = j.get_limit_time() const auto n_nodes =
      j.get_num_nodes();

  auto i = std::max(static_cast<size_t>((t_exec / t_limit) * N), N - 1);
  const slot & = m_tjobs[t_limit];
  slot.m_bins[n_nodes][i]++; // This does not work without initialization
  slot.m_num_jobs++;
}

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_JOB_STAT_TEXEC_HPP
