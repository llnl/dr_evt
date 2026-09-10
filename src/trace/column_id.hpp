/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file column_id.hpp
 * @brief Trace-column identifiers and ordering helpers.
 */

#ifndef DR_EVT_TRACE_COLUMN_ID_HPP
#define DR_EVT_TRACE_COLUMN_ID_HPP

#include "common.hpp"
#include <string>

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 * <column index, column title>
 * Colum index starts from 0.
 * column title can be found in the first line of the data file.
 */
using column_id_t = std::pair<col_no_t, std::string>;

/** @brief Order columns by their zero-based position.
 * @param[in] c1 Left column descriptor.
 * @param[in] c2 Right column descriptor.
 * @return `true` when @p c1 has a smaller position. */
inline bool
operator<(const column_id_t &c1,
          const column_id_t &c2) { // No need for tie-breaking for this problem
  return (c1.first < c2.first);
}

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_COLUMN_ID_HPP
