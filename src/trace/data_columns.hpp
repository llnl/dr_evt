/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file data_columns.hpp
 * @brief Trace-format column selection and header validation.
 */

#ifndef DR_EVT_TRACE_DATA_COLUMNS_HPP
#define DR_EVT_TRACE_DATA_COLUMNS_HPP

#include "common.hpp"
#include "sim/scheduler_policies.hpp" // For TraceMode
#include "trace/column_id.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 * @brief Describes which fields to read from a supported trace format.
 * @details Validates the source header, maps logical names to raw column
 * positions, and extracts only the substrings needed to build Job_Record data.
 */
class Data_Columns {
public:
  /// Ordered logical fields selected from each input row.
  using data_columns_t = std::vector<column_id_t>;

protected:
  /// Map a logical name to its raw-input and filtered-field positions.
  using col_by_name_t =
      std::unordered_map<std::string, std::pair<col_no_t, col_no_t>>;

  /// Ordered filter defining the raw fields retained from each input row.
  data_columns_t m_cols_to_read;

  /// Lookup from logical field name to raw and filtered column indices.
  col_by_name_t m_col_by_name;

  /// Saved process timezone, restored when this mapping is destroyed.
  const char *m_cur_tz;

  /// Number of physical columns declared by the validated header.
  num_cols_t m_total_columns;

  /// Filtered index of the build-selected queue field (`queue` or `q_id`).
  col_no_t m_queue_idx;

#if DR_EVT_LEGACY_QUEUE_INPUT
  /// Whether the legacy input header explicitly contains `queue`.
  bool m_has_queue_column;
#else
  /// Whether the default input header explicitly contains `q_id`.
  bool m_has_q_id_column;
#endif

  /// Raw field excluded from ordinary CSV scanning (for example, user_script).
  std::string m_col_to_avoid;
  /// Raw position of m_col_to_avoid, or the largest index when unused.
  col_no_t m_col_to_avoid_idx;

public:
  /** @brief Construct the default trace-column mapping. */
  Data_Columns();
  /** @brief Construct a mapping for a named trace format.
   * @param[in] format Supported format name, such as "simple" or "lassen". */
  Data_Columns(const std::string &format);
  /** @brief Construct a mapping with timestamp and timezone controls.
   * @param[in] format Supported trace format name.
   * @param[in] timestamp_format Timestamp encoding name.
   * @param[in] timezone Timezone used for timestamps without offsets. */
  Data_Columns(const std::string &format, const std::string &timestamp_format,
               const std::string &timezone);
  /// Restore the process timezone saved during construction.
  virtual ~Data_Columns();

  /// Return the configured timestamp encoding name.
  std::string get_timestamp_format() const { return m_timestamp_format; }
  /// Return the timezone used for timestamps without explicit offsets.
  std::string get_timezone() const { return m_timezone_str; }
  /// Return the simulation or replay mode inferred from the source header.
  TraceMode get_trace_mode() const { return m_trace_mode; }

  /// Allow read-only access to the column filter
  const data_columns_t &get_cols_to_read() const { return m_cols_to_read; }

  /**
   *  Check if the column filter is consistent with the header of data file.
   *  It also detects the total number of columns.
   */
  /** @brief Validate a trace header against this mapping.
   * @param[in] fname Input trace filename.
   * @return true when the header is compatible and column positions were
   * resolved. */
  bool check_header(const std::string &fname);

  /// Return the number of fields retained by the current filter.
  num_cols_t size() const {
    return static_cast<num_cols_t>(m_cols_to_read.size());
  }

  /// Select the substring ranges that matches the columns of interest
  /** @brief Extract ranges for the configured fields from one input row.
   * @param[in] str Raw trace row.
   * @return Substring positions corresponding to selected columns.
   * @details The Lassen format may contain a user-script field with
   * difficult-to-parse content; the configured filter deliberately avoids
   * that raw column before CSV ranges are selected. */
  virtual std::vector<substr_pos_t> pick_values(const std::string &str) const;

  /// Return the index of the column in raw data by name
  /** @brief Return a logical column's index in the raw input row.
   * @param[in] col_name Logical column name.
   * @return Raw column index as col_no_t. */
  col_no_t column_idx_raw(const std::string &col_name) const;

  /** @brief Return a logical column's index in the filtered field list.
   * @param[in] col_name Logical column name.
   * @return Filtered column index as col_no_t. */
  col_no_t column_idx(const std::string &col_name) const;

  /// Return the filtered position of the selected queue field.
  col_no_t get_queue_idx() const { return m_queue_idx; }

#if DR_EVT_LEGACY_QUEUE_INPUT
  /// True when legacy trace input explicitly supplied a `queue` column.
  bool has_queue_column() const { return m_has_queue_column; }
#else
  /// True when default trace input explicitly supplied a `q_id` column.
  bool has_q_id_column() const { return m_has_q_id_column; }
#endif

protected:
  /// Initialize field lookup tables and timezone state from the base mapping.
  void init();

  /// Requested input layout name, such as `simple` or `lassen`.
  std::string m_trace_format;
  /// Requested timestamp encoding, such as `epoch` or `iso`.
  std::string m_timestamp_format;
  /// Timezone for timestamps that do not carry their own offset.
  std::string m_timezone_str;

  /// Trace mode (replay or simulation) detected from columns
  TraceMode m_trace_mode;
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_DATA_COLUMNS_HPP
