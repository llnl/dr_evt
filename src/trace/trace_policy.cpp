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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace dr_evt {

int Standard_Trace_Policy::load_records(const std::string &fname,
                                        const Data_Columns &dcols,
                                        std::vector<record_type> &records,
                                        num_jobs_t max_count,
                                        const Trace_Admission_Limits &limits) {
  return load(fname, dcols, records, max_count, &limits);
}

int Pcon_Trace_Policy::load_records(const std::string &fname,
                                    const Data_Columns &dcols,
                                    std::vector<record_type> &records,
                                    num_jobs_t max_count,
                                    const Trace_Admission_Limits &limits) {
  return load(fname, dcols, records, max_count, &limits);
}

namespace {

/** @brief Parse one numeric SWF token.
 *  @details -1 is a legal "missing" marker, so this is a plain double parse
 *  that requires the whole token to be consumed.
 *  @param[in] token Field text.
 *  @param[in] line_no Line number for the error message.
 *  @return The parsed value.
 *  @throws std::invalid_argument when the token is not a number. */
double parse_swf_num(const std::string &token, size_t line_no) {
  size_t pos = 0;
  double v = 0.0;
  try {
    v = std::stod(token, &pos);
  } catch (const std::exception &) {
    pos = std::string::npos;
  }
  if (pos != token.size()) {
    throw std::invalid_argument("SWF line " + std::to_string(line_no) +
                                ": not a number: '" + token + "'");
  }
  return v;
}

/** @brief Read a "; Key: value" header line.
 *  @details Keys are matched case-insensitively; only the leading number of
 *  the value is read.
 *  @param[in] line Header line including the leading ';'.
 *  @param[in] key Key to match.
 *  @param[out] value Parsed number when the line matches.
 *  @return true when the line carries @p key with a numeric value. */
bool parse_swf_header_value(const std::string &line, const char *key,
                            double &value) {
  const size_t colon = line.find(':');
  if (colon == std::string::npos) {
    return false;
  }
  std::string k = trim(line.substr(1, colon - 1));
  std::string want(key);
  if (k.size() != want.size()) {
    return false;
  }
  for (size_t i = 0; i < k.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(k[i])) !=
        std::tolower(static_cast<unsigned char>(want[i]))) {
      return false;
    }
  }
  // Archive headers often annotate the value, e.g.
  // "; MaxNodes: 338 (batch partition; 512 total)", so take the leading
  // number and ignore whatever follows it.
  try {
    size_t pos = 0;
    const std::string v = trim(line.substr(colon + 1));
    value = std::stod(v, &pos);
    return pos > 0;
  } catch (const std::exception &) {
    return false;
  }
}

} // namespace

unsigned Swf_Trace_Policy::header_max_nodes(const std::string &fname) {
  std::ifstream input(fname);
  std::string line;
  double v = 0.0;
  while (input && std::getline(input, line)) {
    if (line.empty() || line.front() != ';') {
      if (!line.empty() &&
          line.find_first_not_of(" \t\r") != std::string::npos) {
        break; // first data line: header is over
      }
      continue;
    }
    if (parse_swf_header_value(line, "MaxNodes", v) && v > 0.0 &&
        v <= static_cast<double>(std::numeric_limits<unsigned>::max())) {
      return static_cast<unsigned>(v);
    }
  }
  return 0u;
}

int Swf_Trace_Policy::load_records(const std::string &fname,
                                   const Data_Columns &dcols,
                                   std::vector<record_type> &records,
                                   num_jobs_t max_count,
                                   const Trace_Admission_Limits &limits) {
  std::ifstream input(fname);
  if (!input) {
    std::cerr << "Failed to open SWF trace: " << fname << std::endl;
    return EXIT_FAILURE;
  }
  if (max_count == static_cast<num_jobs_t>(0u)) {
    max_count = std::numeric_limits<num_jobs_t>::max();
  }

  // Header metadata. Only these three keys matter to the simulator.
  double unix_start = 0.0;
  double max_nodes = 0.0;
  double max_procs = 0.0;

  const size_t first_new = records.size();
  num_jobs_t accepted = 0;
  num_jobs_t skipped = 0;
  num_jobs_t clamped = 0;
  num_jobs_t dropped = 0;
  num_jobs_t data_row = 0;
  size_t line_no = 0;
  std::string line;
  // Fixed once the first data line is seen, so a header key that appears
  // after data (not valid SWF, but harmless) cannot change the mapping.
  bool divisor_fixed = false;
  double cores_per_node = 1.0;

  while (accepted < max_count && std::getline(input, line)) {
    ++line_no;
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
                             line.back() == '\r' || line.back() == '\n')) {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    if (line.front() == ';') {
      double v = 0.0;
      if (parse_swf_header_value(line, "UnixStartTime", v)) {
        unix_start = v;
      } else if (parse_swf_header_value(line, "MaxNodes", v)) {
        max_nodes = v;
      } else if (parse_swf_header_value(line, "MaxProcs", v)) {
        max_procs = v;
      }
      continue;
    }
    ++data_row;
    if (!divisor_fixed) {
      const unsigned override = dcols.cores_per_node_override();
      if (override > 0u) {
        cores_per_node = static_cast<double>(override);
      } else if (max_nodes > 0.0 && max_procs > 0.0 && max_procs >= max_nodes) {
        cores_per_node = std::floor(max_procs / max_nodes);
      } else {
        cores_per_node = 1.0;
      }
      divisor_fixed = true;
    }

    const auto tokens = whitespace_separate(line);
    if (tokens.size() < 18u) {
      throw std::invalid_argument("SWF line " + std::to_string(line_no) +
                                  " has " + std::to_string(tokens.size()) +
                                  " fields, expected 18");
    }
    auto field = [&](size_t idx) { // idx is the 1-based SWF field number
      const auto &pos = tokens[idx - 1u];
      return parse_swf_num(line.substr(pos.first, pos.second), line_no);
    };
    const double job_number = field(1);
    const double submit_rel = field(2);
    const double run_time = field(4);
    const double procs = field(5);
    const double requested = field(9);
    const double queue_no = field(15);

    if (procs <= 0.0 || run_time <= 0.0) {
      ++skipped; // cancelled before start, or no usable size/duration
      continue;
    }

    // Archive logs record jobs that ran past their requested time (sites
    // grant grace periods). The simulator kills a job at its limit, and a
    // stored actual_run_time above time_limit is rejected by the CSV loader,
    // so truncate to the limit and count it.
    double run_time_used = run_time;
    const double limit_for_run = requested > 0.0 ? std::ceil(requested) : 0.0;
    if (limit_for_run > 0.0 && run_time_used > limit_for_run) {
      run_time_used = limit_for_run;
      ++clamped;
    }
    const double submit = unix_start + submit_rel;
    const time_t submit_sec = static_cast<time_t>(std::floor(submit));
    const epoch_t submit_epoch = {
        submit_sec,
        static_cast<float>(submit - static_cast<double>(submit_sec))};
    const double nodes_d = std::ceil(procs / cores_per_node);
    const double limit_d = std::ceil(requested > 0.0 ? requested : run_time);
    if (nodes_d >
            static_cast<double>(std::numeric_limits<num_nodes_t>::max()) ||
        limit_d > static_cast<double>(std::numeric_limits<timeout_t>::max())) {
      throw std::invalid_argument("SWF line " + std::to_string(line_no) +
                                  ": node count or time limit out of range");
    }
    Job_Record job(submit_epoch, static_cast<num_nodes_t>(nodes_d), Queue1,
                   static_cast<timeout_t>(limit_d));
    job.set_actual_run_time(static_cast<tdiff_t>(run_time_used));

    const std::uint32_t swf_no =
        (job_number >= 0.0 && job_number <= 4294967295.0)
            ? static_cast<std::uint32_t>(job_number)
            : 0u;
    const std::uint8_t swf_q = (queue_no >= 1.0 && queue_no <= 255.0)
                                   ? static_cast<std::uint8_t>(queue_no)
                                   : static_cast<std::uint8_t>(0u);
    if (limits.maximum_nodes && job.get_num_nodes() > *limits.maximum_nodes) {
      // Same admission rule and diagnostic as the CSV loaders: a request
      // larger than the system can never be scheduled, so it never receives
      // a simulation job ID.
      if (limits.report_dropped_jobs) {
        std::cerr << "Dropped trace row " << data_row << " (submit_time="
                  << dr_evt::to_string(job.get_submit_time())
                  << ", nodes=" << job.get_num_nodes()
                  << "): request exceeds maximum allowed nodes ("
                  << *limits.maximum_nodes << ")\n";
      }
      ++dropped;
      continue;
    }
    records.emplace_back(std::move(job), swf_no, swf_q);
    ++accepted;
  }

  // Archive files are nearly always in submit order, but ties and the odd
  // out-of-order line occur. Sort what this call appended so the progressive
  // loader's per-file ordering check holds; load_data()'s own stable sort
  // then has nothing left to do.
  std::stable_sort(records.begin() + static_cast<std::ptrdiff_t>(first_new),
                   records.end());

  dcols.note_swf_load(static_cast<unsigned>(cores_per_node), skipped);
  std::cerr << "SWF: loaded " << accepted << " records, skipped " << skipped
            << " (processors<=0 or run_time<=0), cores_per_node="
            << static_cast<unsigned>(cores_per_node) << ", run time truncated"
            << " to the requested time for " << clamped << ", dropped "
            << dropped << " above the node limit" << std::endl;
  return EXIT_SUCCESS;
}

} // namespace dr_evt
