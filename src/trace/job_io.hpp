/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file job_io.hpp
 * @brief Job-trace input and diagnostic output helpers.
 */

#ifndef DR_EVT_TRACE_JOB_IO_HPP
#define DR_EVT_TRACE_JOB_IO_HPP

#include <string>
#include <vector>
#include <iostream>
#include "common.hpp"
#include "trace/column_id.hpp"
#include "trace/data_columns.hpp"
#include "trace/job_record.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 * @brief Load Job_Record values from a trace file.
 * @param[in] fname Input trace filename.
 * @param[in] dcols Validated column mapping for the trace format.
 * @param[out] data Destination records appended from the file.
 * @param[in] max_cnt Maximum records to load; zero means no explicit limit.
 * @return EXIT_SUCCESS on success, otherwise a nonzero error code.
 */
int load(const std::string& fname,
          const Data_Columns& dcols,
          std::vector<Job_Record>& data,
          num_jobs_t max_cnt = static_cast<num_jobs_t>(0u));

/** @brief Write a collection of jobs in tabular trace form.
 * @tparam JobContainer Container supporting empty(), front(), and iteration.
 * @param[in,out] os Destination stream.
 * @param[in] data Job records to write.
 * @return The destination stream after writing.
 * @details Templated so both std::vector<Job_Record> (tracer) and
/// boost::circular_buffer<Job_Record> (Trace::m_data) work - only uses
/// operations both support (empty(), front(), range-based iteration). */
template <typename JobContainer>
std::ostream& print(std::ostream& os, const JobContainer& data)
{
    os << "no.\t" + Job_Record::get_header_str() << std::endl;

    if (data.empty()) {
        return os;
    }

    const size_t line_sz = data.front().to_string().size();
    const size_t line_sz_est = ((line_sz == 0ul)? 1ul : line_sz);
    const size_t blk_sz = 65536ul;

    const size_t lines_to_buf = (blk_sz + line_sz_est - 1) / line_sz_est;
    size_t buf_cnt = 1ul;
    std::string buf;
    buf.reserve(blk_sz + 4096);

    num_jobs_t cnt = static_cast<num_jobs_t>(0u);

    for (const auto& job: data) {
      #if !INCLUDE_DAT
        if (_Is_Exclusive(job.get_queue())) {
            continue;
        }
      #endif
        buf += std::to_string(++ cnt) + '\t' + job.to_string() + '\n';
        if (buf_cnt == lines_to_buf) {
            os << buf;
            buf_cnt = 1ul;
            buf.clear();
        } else {
            buf_cnt ++;
        }
    }

    if (!buf.empty()) {
        os << buf;
        buf_cnt = 0ul;
        buf.clear();
    }

    return os;
}

/** @brief Print a diagnostic comparison of requested and actual runtime.
 * @param[in] data Job records to analyze. */
void print_limit_vs_exec_time(const std::vector<Job_Record>& data);

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_JOB_IO_HPP
