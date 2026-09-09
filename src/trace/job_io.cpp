/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file job_io.cpp
 * @brief Job-trace input and diagnostic output implementation.
 */

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <limits>
#include <iterator>

#include "trace/job_io.hpp"
#include "trace/parse_utils.hpp"

namespace dr_evt {

using std::cout;
using std::endl;
using std::vector;
using std::string;


int load(const string& fname,
         const Data_Columns& dcols,
         vector<Job_Record>& data,
         num_jobs_t max_cnt)
{
    if (fname.empty()) {
        return EXIT_FAILURE;
    }

    std::ifstream ifs(fname);
    if (!ifs) {
        return EXIT_FAILURE;
    }

    const auto& columns_to_read = dcols.get_cols_to_read();
    const auto record_sz = columns_to_read.size();
    const auto q_idx = dcols.get_queue_idx();
    const bool has_queue_column = dcols.has_queue_column();

    if (columns_to_read.empty()) {
        std::cerr << "no column to read!" << std::endl;
        return EXIT_SUCCESS;
    }

    string line;
    std::getline(ifs, line); // Consume the header line

    if (max_cnt == static_cast<num_jobs_t>(0u)) {
        max_cnt = std::numeric_limits<num_jobs_t>::max();
    }
    num_jobs_t cnt = static_cast<num_jobs_t>(0u);

    while (std::getline(ifs, line)) { // Read a line
        if (cnt++ >= max_cnt) {
            break;
        }

        // Strip trailing whitespace (including \r from Windows line endings)
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
                                  line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }

        vector<string> rec_str;
        rec_str.reserve(record_sz);

        auto val_pos = dcols.pick_values(line);
      #if !SHOW_ALL_QUEUE
        // A queue-less trace is assigned pbatch below, so it is always in
        // the queue of interest under the default filtering configuration.
        bool right_q = !has_queue_column; // queue of interest
      #endif // !SHOW_ALL_QUEUE

        for (auto col_idx = static_cast<col_no_t>(0u); col_idx < record_sz; col_idx ++)
        { // Check the job queue
            const auto& pos = val_pos [col_idx];
            string substr = line.substr(pos.first, pos.second);
          #if !SHOW_ALL_QUEUE
            if (col_idx == q_idx) { // Check the job queue
              #if INCLUDE_DAT || MARK_DAT_PERIOD
                bool q1 = false, q2 = false;
               #if 0 // Case-insensitive search
                right_q = (q1 = search_ci(substr, "pbatch")) ||
                          (q2 = search_ci(substr, "pall"));
               #else
                right_q = (q1 = (substr.find("pbatch") != string::npos)) ||
                          (q2 = (substr.find("pall") != string::npos));
               #endif
                if (!right_q) {
                    break;
                }
              #else
                right_q = search_ci(substr, "pbatch");
                if (!right_q) break;
              #endif
            }
          #endif // !SHOW_ALL_QUEUE
            rec_str.emplace_back(trim(substr));
        }

      #if !SHOW_ALL_QUEUE
        if (!right_q) {
            continue;
        }
      #endif // !SHOW_ALL_QUEUE

        if (!has_queue_column) {
            // Job_Record has one canonical layout in each mode.  Keep the
            // omitted source field out of the output schema, but insert its
            // default value for the internal record.
            const auto queue_pos = dcols.get_trace_mode() == TraceMode::REPLAY
                ? 4u : 2u;
            rec_str.insert(rec_str.begin() + queue_pos, "pbatch");
        }

        try {
            // Constructor may raise an exception based on filtering.
            // In that case, it can be handled as below to ignore this
            // particular sample that is not compliant.
          #if SHOW_ORG_NO
            // line number starts from 1
            data.push_back(Job_Record(cnt, rec_str));
          #else
            data.push_back(Job_Record(rec_str));
          #endif
        } catch (std::domain_error& e) {
            // Ignore this case
            std::cerr << std::string(e.what())
                + ": [" + std::to_string(cnt) + "]" << endl;
        } catch (std::exception& e) {
            std::ostringstream oss_err;
            oss_err << e.what() << ": [" << cnt << "] " << line << endl;
            throw std::invalid_argument {oss_err.str().c_str()};
        }
    }
    ifs.close();

    return EXIT_SUCCESS;
}

void print_limit_vs_exec_time(const vector<Job_Record>& data)
{
    cout << "limit" << "\t" << "exec" << endl;

    using lim_exec_t = std::pair<timeout_t, tdiff_t>;
    vector<lim_exec_t> data_focus(data.size());

    for (num_jobs_t i = static_cast<num_jobs_t>(0u); i < data.size(); ++i) {
         const auto& job = data[i];
         data_focus[i] = std::make_pair(job.get_limit_time(), job.get_actual_run_time());
    }

    std::stable_sort(data_focus.begin(), data_focus.end(),
        [](const lim_exec_t& lhs, const lim_exec_t& rhs) -> bool
        {
            return ((lhs.first < rhs.first) ||
                    ((lhs.first == rhs.first) &&
                     (lhs.second < rhs.second)));
        }
    );

    for (const auto& job: data_focus) {
        cout << job.first << '\t' << job.second << endl;
    }
}

} // end of namespace dr_evt
