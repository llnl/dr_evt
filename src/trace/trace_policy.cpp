/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file trace_policy.cpp
 * @brief Input adapters for the explicitly supported trace policies.
 */

#include "trace/trace_policy.hpp"

#include "trace/data_columns.hpp"
#include "trace/job_io.hpp"
#include "trace/parse_utils.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace dr_evt {

int Standard_Trace_Policy::load_records(const std::string &fname,
                                        const Data_Columns &dcols,
                                        std::vector<record_type> &records,
                                        num_jobs_t max_count) {
  return load(fname, dcols, records, max_count);
}

int Pcon_Trace_Policy::load_records(const std::string &fname,
                                    const Data_Columns &dcols,
                                    std::vector<record_type> &records,
                                    num_jobs_t max_count) {
  std::vector<Job_Record> jobs;
  const int rc = load(fname, dcols, jobs, max_count);
  if (rc != EXIT_SUCCESS) {
    return rc;
  }

  std::ifstream input(fname);
  if (!input) {
    return EXIT_FAILURE;
  }

  std::string line;
  if (!std::getline(input, line)) {
    return EXIT_FAILURE;
  }

  std::unordered_map<std::string, size_t> columns;
  const auto header_fields = comma_separate(line);
  for (size_t i = 0; i < header_fields.size(); ++i) {
    columns.emplace(
        trim(line.substr(header_fields[i].first, header_fields[i].second)), i);
  }

  auto required_index = [&columns](const char *name) {
    const auto found = columns.find(name);
    if (found == columns.end()) {
      throw std::invalid_argument(std::string("Pcon trace requires column '") +
                                  name + "'");
    }
    return found->second;
  };
  const size_t avg_index = required_index("avgpcon");
  const size_t min_index = required_index("minpcon");
  const size_t max_index = required_index("maxpcon");

  if (max_count == static_cast<num_jobs_t>(0u)) {
    max_count = std::numeric_limits<num_jobs_t>::max();
  }

  records.reserve(jobs.size());
  num_jobs_t count = 0;
  while (count < max_count && records.size() < jobs.size() &&
         std::getline(input, line)) {
    ++count;
    const auto fields = comma_separate(line);
    if (avg_index >= fields.size() || min_index >= fields.size() ||
        max_index >= fields.size()) {
      throw std::invalid_argument("Pcon trace row " + std::to_string(count) +
                                  " has fewer columns than its header");
    }
    Pcon_Values pcon;
    set_by(pcon.avgpcon, trim(line.substr(fields[avg_index].first,
                                          fields[avg_index].second)));
    set_by(pcon.minpcon, trim(line.substr(fields[min_index].first,
                                          fields[min_index].second)));
    set_by(pcon.maxpcon, trim(line.substr(fields[max_index].first,
                                          fields[max_index].second)));
    records.emplace_back(std::move(jobs[records.size()]), pcon);
  }

  if (records.size() != jobs.size()) {
    throw std::invalid_argument(
        "Pcon trace parsing produced a different number of experimental "
        "values and job records");
  }

  return EXIT_SUCCESS;
}

} // namespace dr_evt
