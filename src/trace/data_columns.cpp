/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cassert>
#include <map>
#include "trace/data_columns.hpp"
#include "trace/job_record.hpp"
#include "trace/parse_utils.hpp"

namespace dr_evt {

Data_Columns::Data_Columns()
  : m_cur_tz(nullptr),
    m_total_columns(static_cast<num_cols_t>(0u)),
    m_queue_idx(static_cast<col_no_t>(0u)),
    m_trace_format("lassen"),
    m_timestamp_format("iso"),
    m_timezone_str("America/Los_Angeles"),
    m_trace_mode(TraceMode::REPLAY)  // Default to replay
{
    // TODO: This should be read from an input file
    // Define the data columns to read. The rest will be not collected to
    // fill out a job record object. However, they might still be used in
    // filtering.
    m_cols_to_read = {
        {11, "num_nodes"}, {23, "begin_time"}, {24, "end_time"},
        {29, "job_submit_time"}, {30, "queue"}, {32, "time_limit"}
    };
    m_col_to_avoid = "user_script";
    init();
}

Data_Columns::Data_Columns(const std::string& format)
  : m_cur_tz(nullptr),
    m_total_columns(static_cast<num_cols_t>(0u)),
    m_queue_idx(static_cast<col_no_t>(0u)),
    m_trace_format(format),
    m_timestamp_format("iso"),
    m_timezone_str("America/Los_Angeles"),
    m_trace_mode(TraceMode::REPLAY)  // Will be detected in check_header
{
    if (format == "simple") {
        // Simple format file columns: [job_submit_time, begin_time, end_time, num_nodes, exit_status, queue, time_limit, actual_duration (optional)]
        // Job_Record expects: [num_nodes, begin_time, end_time, job_submit_time, queue, time_limit, actual_duration]
        // After sorting by column index, we need to remap to Job_Record order
        // We define in Job_Record's expected order here, but need different column indices:
        m_cols_to_read = {
            {3, "num_nodes"},        // File column 3 -> Job_Record field 0
            {1, "begin_time"},       // File column 1 -> Job_Record field 1
            {2, "end_time"},         // File column 2 -> Job_Record field 2
            {0, "job_submit_time"},  // File column 0 -> Job_Record field 3
            {5, "queue"},            // File column 5 -> Job_Record field 4
            {6, "time_limit"},       // File column 6 -> Job_Record field 5
            {7, "actual_duration"}   // File column 7 -> Job_Record field 6 (optional)
        };
        m_col_to_avoid = "";  // No problematic columns in simple format
    } else {
        // Lassen format (default)
        m_cols_to_read = {
            {11, "num_nodes"}, {23, "begin_time"}, {24, "end_time"},
            {29, "job_submit_time"}, {30, "queue"}, {32, "time_limit"}
        };
        m_col_to_avoid = "user_script";
    }
    init();
}

Data_Columns::Data_Columns(const std::string& format, const std::string& timestamp_format, const std::string& timezone)
  : m_cur_tz(nullptr),
    m_total_columns(static_cast<num_cols_t>(0u)),
    m_queue_idx(static_cast<col_no_t>(0u)),
    m_trace_format(format),
    m_timestamp_format(timestamp_format),
    m_timezone_str(timezone),
    m_trace_mode(TraceMode::REPLAY)  // Will be detected in check_header
{
    if (format == "simple") {
        // Simple format: [arrival_time, start_time, end_time, num_nodes, exit_status, queue, time_limit, actual_duration (optional)]
        m_cols_to_read = {
            {3, "num_nodes"}, {1, "begin_time"}, {2, "end_time"},
            {0, "job_submit_time"}, {5, "queue"}, {6, "time_limit"}, {7, "actual_duration"}
        };
        m_col_to_avoid = "";
    } else {
        // Lassen format
        m_cols_to_read = {
            {11, "num_nodes"}, {23, "begin_time"}, {24, "end_time"},
            {29, "job_submit_time"}, {30, "queue"}, {32, "time_limit"}
        };
        m_col_to_avoid = "user_script";
    }
    init();
}

Data_Columns::~Data_Columns()
{
    // Restore the original timezone
     if (m_cur_tz != nullptr) {
        setenv("TZ", m_cur_tz, 1);
     } else {
        unsetenv("TZ");
     }
    tzset();
    if (m_cur_tz != nullptr) {
        delete m_cur_tz;
        m_cur_tz = nullptr;
    }
}

void Data_Columns::init()
{
    for (auto i = static_cast<num_cols_t>(0u); i < m_cols_to_read.size(); ++i) {
        const auto& c = m_cols_to_read[i];
        const auto result
            = m_col_by_name.insert(
                  std::make_pair(c.second,
                                 std::make_pair(c.first, i)));

        if (!result.second) {
            std::string err("Possible duplicate column name with " + c.second);
            throw std::invalid_argument {err.c_str()};
        }
        if (c.second == "queue") {
            m_queue_idx = i;
        }
    }

    // Make sure the columns are in the order of increasing index
    // NOTE: For simple format, we keep them in Job_Record's expected order, not file order
    if (m_trace_format != "simple") {
        std::sort(m_cols_to_read.begin(), m_cols_to_read.end());
    }

    Job_Record::set_num_inputs(static_cast<unsigned>(size()));

    // Set timezone to the zone where data was collected.
    // This will be used in converting time strings to determine
    // the daylight saving condition.

    if (m_cur_tz != nullptr) {
        delete m_cur_tz;
        m_cur_tz = nullptr;
    }

    const char* tz = getenv("TZ");
    if (tz != nullptr) {
        auto tz_str_len = strlen(tz);
        m_cur_tz = (char*) calloc((tz_str_len + 1), sizeof(char));
        memcpy((void*) m_cur_tz, (void*) tz, tz_str_len * sizeof(char));
    }

    setenv("TZ", DATA_TIMEZONE, 1);
    tzset();
}

bool Data_Columns::check_header(const std::string& fname)
{
    if (fname.empty()) {
        return false;
    }

    std::ifstream ifs(fname);
    if (!ifs) {
        return false;
    }

    if (m_cols_to_read.empty()) {
        return true;
    }

    std::string line;
    std::getline(ifs, line); // Read the header line
    std::istringstream header(line);
    std::vector<std::string> col_names;

    auto idx = static_cast<col_no_t>(0u);
    while (header.good()) { // Find the number of columns
        std::string substr;
        std::getline(header, substr, ',');

        // Strip trailing whitespace (including \r from Windows line endings)
        while (!substr.empty() && (substr.back() == ' ' || substr.back() == '\t' ||
                                    substr.back() == '\r' || substr.back() == '\n')) {
            substr.pop_back();
        }

        col_names.emplace_back(substr);

        if (substr == m_col_to_avoid) {
            m_col_to_avoid_idx = idx;
            std::cerr << "Avoid parsing " + substr << std::endl;
        }
        idx ++;
    }

    // Build column name to index map from actual header
    std::map<std::string, col_no_t> col_map;
    for (col_no_t i = 0; i < col_names.size(); ++i) {
        col_map[col_names[i]] = i;
    }

    // Detect trace mode from columns present
    bool has_begin_time = col_map.find("begin_time") != col_map.end();
    bool has_end_time = col_map.find("end_time") != col_map.end();

    if (has_begin_time && has_end_time) {
        m_trace_mode = TraceMode::REPLAY;
    } else if (!has_begin_time && !has_end_time) {
        m_trace_mode = TraceMode::SIMULATION;
    } else {
        throw std::invalid_argument("Ambiguous trace format: has one of begin_time/end_time but not both");
    }

    // Rebuild m_cols_to_read with actual column indices from header
    m_cols_to_read.clear();

    if (m_trace_mode == TraceMode::REPLAY) {
        // Replay mode: need all columns including begin_time and end_time
        m_cols_to_read = {
            {col_map["num_nodes"], "num_nodes"},
            {col_map["begin_time"], "begin_time"},
            {col_map["end_time"], "end_time"},
            {col_map["job_submit_time"], "job_submit_time"},
            {col_map["queue"], "queue"},
            {col_map["time_limit"], "time_limit"}
        };
    } else {
        // Simulation mode: no begin_time or end_time
        auto find_column = [&col_map](const std::string& name) -> col_no_t {
            auto it = col_map.find(name);
            if (it == col_map.end()) {
                throw std::invalid_argument("Required column '" + name + "' not found in trace file");
            }
            return it->second;
        };

        col_no_t num_nodes_idx = find_column("num_nodes");
        col_no_t submit_time_idx = find_column("job_submit_time");
        col_no_t queue_idx = find_column("queue");
        col_no_t time_limit_idx = find_column("time_limit");

        m_cols_to_read = {
            {num_nodes_idx, "num_nodes"},
            {submit_time_idx, "job_submit_time"},
            {queue_idx, "queue"},
            {time_limit_idx, "time_limit"}
        };

        // Optional: actual_duration column for FROM_COLUMN mode
        auto actual_duration_it = col_map.find("actual_duration");
        if (actual_duration_it != col_map.end()) {
            m_cols_to_read.push_back({actual_duration_it->second, "actual_duration"});
        };
    }

    // Verify all required columns are present
    for (const auto& c: m_cols_to_read) {
        const std::string& col_name = c.second;
        if (col_map.find(col_name) == col_map.end()) {
            std::string err_str =
                "Required column '" + col_name + "' is not present!";
            throw std::invalid_argument {err_str};
            return false;
        }
    }
    m_total_columns = static_cast<num_cols_t>(col_names.size());

    // Sort columns by file index for proper extraction
    // Exception: for simple format, keep in Job_Record expected order
    if (m_trace_format != "simple") {
        std::sort(m_cols_to_read.begin(), m_cols_to_read.end());
    }

    // Reinitialize after rebuilding m_cols_to_read
    m_col_by_name.clear();

    for (auto i = static_cast<num_cols_t>(0u); i < m_cols_to_read.size(); ++i) {
        const auto& c = m_cols_to_read[i];
        const auto result
            = m_col_by_name.insert(
                  std::make_pair(c.second,
                                 std::make_pair(c.first, i)));

        if (!result.second) {
            std::string err("Possible duplicate column name with " + c.second);
            throw std::invalid_argument {err.c_str()};
        }
        if (c.second == "queue") {
            m_queue_idx = i;
        }
    }

    // Update Job_Record with correct field count
    Job_Record::set_num_inputs(static_cast<unsigned>(m_cols_to_read.size()));

    return true;
}

/**
 *  This function is data-specific.
 *  The Lassen trace file contains for example 'user_script' field, which is
 *  23rd. This field contains many characters that would make parsing difficult.
 *  e.g., ',', ';', '"', "'", '\' and '#'.
 *  Especially, if it contains ','. There are more fields that sometimes
 *  contain such characters in their values as well.
 *  Therefore, we first check the number of comma-separated values in a line.
 *  If it is larger than expected, we try removing commas in values.
 *  Then, we detect the position of values of the columns after 'user_script'
 *  from the back of the string towards the problematic one.
 */
std::vector<substr_pos_t>
Data_Columns::pick_values(const std::string& str) const
{
    auto col_pos = comma_separate(str);
    // col_pos.size() should not be less than m_total_columns
    // If it is larger, it is due to the non-delimiter commas in a value.
    if (col_pos.size() < m_total_columns) {
        std::string err_str
            = "The number of comma-separated values are "
              "less than the total number of columns: "
            + std::to_string(col_pos.size())
            + " < " + std::to_string(m_total_columns);
        throw std::length_error(err_str);
    }

    if (col_pos.size() > m_total_columns) {
        auto str_cpy = str;
        replace_comma_within_quotation(str_cpy);
        col_pos = comma_separate(str_cpy);
    }

    const auto sz = static_cast<num_cols_t>(m_cols_to_read.size());
    col_no_t i = static_cast<col_no_t>(0u);
    std::vector<substr_pos_t> val_pos(sz);

    for (; i < sz; ++i) {
        const auto& c = m_cols_to_read[i];
        if (c.first >= m_col_to_avoid_idx) break;
        val_pos[i] = col_pos[c.first];
    }

    for (col_no_t j = sz; i < j; ) {
        const auto& c = m_cols_to_read[--j];
        val_pos[j] = col_pos[c.first];
    }

    return val_pos;
}

col_no_t Data_Columns::column_idx_raw(const std::string& col_name) const
{
    const auto& it = m_col_by_name.find(col_name);
    if (it == m_col_by_name.cend()) {
        std::string err = "Unknown column name: " + col_name;
        throw std::invalid_argument {err.c_str()};
    }
    return it->second.first;
}

col_no_t Data_Columns::column_idx(const std::string& col_name) const
{
    const auto& it = m_col_by_name.find(col_name);
    if (it == m_col_by_name.cend()) {
        std::string err = "Unknown column name: " + col_name;
        throw std::invalid_argument {err.c_str()};
    }
    return it->second.second;
}

} // end of namespace dr_evt
