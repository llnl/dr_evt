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

#include <string>
#include <vector>
#include <unordered_map>
#include "common.hpp"
#include "trace/column_id.hpp"
#include "sim/scheduler_policies.hpp"  // For TraceMode

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
    using data_columns_t = std::vector<column_id_t>;

  protected:
    /// Map a name to the column index in the raw data and that in the filtered
    using col_by_name_t = std::unordered_map<std::string,
                                             std::pair<col_no_t, col_no_t>>;

    /**
     * Column filter defines the columns to read.
     * The rest will be filtered out.
     */
    data_columns_t m_cols_to_read;

    /// maps a column name to an index to the filter
    col_by_name_t m_col_by_name;

    /**
     * The current timezone is backed up before processing and restored after.
     * The timezone information is needed to determine daylight saving in case
     * that the timestamps in data are missing timezone info.
     * In that case, the timezone of the data needs to be set as the macro value
     * DATA_TIMEZONE in common.h
     */
    const char* m_cur_tz;

    /// Total number of columns in the data file checked against
    num_cols_t m_total_columns;

    /// The index of the column_id entry for queue
    col_no_t m_queue_idx;

  #if DR_EVT_LEGACY_QUEUE_INPUT
    /// Whether the legacy input header explicitly contains `queue`.
    bool m_has_queue_column;
  #else
    /// Whether the default input header explicitly contains `q_id`.
    bool m_has_q_id_column;
  #endif

    /// A particular column that is extrememly difficult to parse.
    std::string m_col_to_avoid;
    /// Column index of the m_col_to_avoid in the raw data
    col_no_t m_col_to_avoid_idx;

  public:
    /** @brief Construct the default trace-column mapping. */
    Data_Columns();
    /** @brief Construct a mapping for a named trace format.
     * @param[in] format Supported format name, such as "simple" or "lassen". */
    Data_Columns(const std::string& format);
    /** @brief Construct a mapping with timestamp and timezone controls.
     * @param[in] format Supported trace format name.
     * @param[in] timestamp_format Timestamp encoding name.
     * @param[in] timezone Timezone used for timestamps without offsets. */
    Data_Columns(const std::string& format, const std::string& timestamp_format, const std::string& timezone);
    virtual ~Data_Columns();

    std::string get_timestamp_format() const { return m_timestamp_format; }
    std::string get_timezone() const { return m_timezone_str; }
    TraceMode get_trace_mode() const { return m_trace_mode; }

    /// Allow read-only access to the column filter
    const data_columns_t& get_cols_to_read() const { return m_cols_to_read; }

    /**
     *  Check if the column filter is consistent with the header of data file.
     *  It also detects the total number of columns.
     */
    /** @brief Validate a trace header against this mapping.
     * @param[in] fname Input trace filename.
     * @return true when the header is compatible and column positions were resolved. */
    bool check_header(const std::string& fname);

    /// Return the number of columns
    num_cols_t size() const { return static_cast<num_cols_t>(m_cols_to_read.size()); }

    /// Select the substring ranges that matches the columns of interest
    /** @brief Extract ranges for the configured fields from one input row.
     * @param[in] str Raw trace row.
     * @return Substring positions corresponding to selected columns.
     * @details The Lassen format may contain a user-script field with
     * difficult-to-parse content; the configured filter deliberately avoids
     * that raw column before CSV ranges are selected. */
    virtual std::vector<substr_pos_t> pick_values(const std::string& str) const;

    /// Return the index of the column in raw data by name
    /** @brief Return a logical column's index in the raw input row.
     * @param[in] col_name Logical column name.
     * @return Raw column index as col_no_t. */
    col_no_t column_idx_raw(const std::string& col_name) const;

    /** @brief Return a logical column's index in the filtered field list.
     * @param[in] col_name Logical column name.
     * @return Filtered column index as col_no_t. */
    col_no_t column_idx(const std::string& col_name) const;

    col_no_t get_queue_idx() const { return m_queue_idx; }

  #if DR_EVT_LEGACY_QUEUE_INPUT
    /// True when legacy trace input explicitly supplied a `queue` column.
    bool has_queue_column() const { return m_has_queue_column; }
  #else
    /// True when default trace input explicitly supplied a `q_id` column.
    bool has_q_id_column() const { return m_has_q_id_column; }
  #endif

  protected:
    void init();

    std::string m_trace_format;      // "simple" or "lassen"
    std::string m_timestamp_format;  // "epoch" or "iso"
    std::string m_timezone_str;  // Timezone string

    /// Trace mode (replay or simulation) detected from columns
    TraceMode m_trace_mode;
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_DATA_COLUMNS_HPP
