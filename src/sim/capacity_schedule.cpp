/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file capacity_schedule.cpp
 * @brief Parsing and validation of scheduled cluster capacity changes.
 * @details Reads the capacity schedule shared by simulation events and
 * reservation forecasts. This implements input validation, not queue order,
 * timing, or backfill policy.
 * @note Enable with --capacity_schedule FILE and set the maximum with
 * --total_nodes N. Simulation_Params uses capacity_schedule and total_nodes;
 * InitRequest has no capacity_schedule field.
 */

#include "sim/capacity_schedule.hpp"
#include "trace/parse_utils.hpp"
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace dr_evt {

std::vector<Capacity_Change>
load_capacity_schedule(const std::string &filename,
                       num_nodes_t configured_maximum) {
  if (filename.empty()) {
    return {};
  }

  std::ifstream input(filename);
  if (!input) {
    throw std::runtime_error("Failed to open capacity schedule: " + filename);
  }

  std::string line;
  if (!std::getline(input, line)) {
    throw std::invalid_argument("Capacity schedule is empty: " + filename);
  }

  std::unordered_map<std::string, size_t> columns;
  const auto header = comma_separate(line);
  for (size_t i = 0; i < header.size(); ++i) {
    columns.emplace(trim(line.substr(header[i].first, header[i].second)), i);
  }
  const auto time_it = columns.find("time");
  const auto nodes_it = columns.find("total_nodes");
  if (time_it == columns.end() || nodes_it == columns.end()) {
    throw std::invalid_argument(
        "Capacity schedule requires CSV columns 'time,total_nodes'");
  }

  std::vector<Capacity_Change> changes;
  TimestampEncoding timestamp_encoding = TimestampEncoding::EPOCH;
  bool timestamp_encoding_detected = false;
  size_t row = 1;
  while (std::getline(input, line)) {
    ++row;
    if (trim(line).empty()) {
      continue;
    }
    const auto fields = comma_separate(line);
    if (time_it->second >= fields.size() || nodes_it->second >= fields.size()) {
      throw std::invalid_argument("Capacity schedule row " +
                                  std::to_string(row) +
                                  " has fewer fields than its header");
    }

    epoch_t parsed_time;
    unsigned parsed_nodes = 0;
    try {
      const std::string time_value = trim(line.substr(
          fields[time_it->second].first, fields[time_it->second].second));
      if (!timestamp_encoding_detected) {
        timestamp_encoding = detect_timestamp_encoding(time_value);
        timestamp_encoding_detected = true;
      }
      set_by(parsed_time, time_value, timestamp_encoding);
      set_by(parsed_nodes, trim(line.substr(fields[nodes_it->second].first,
                                            fields[nodes_it->second].second)));
    } catch (const std::exception &e) {
      throw std::invalid_argument("Invalid capacity schedule row " +
                                  std::to_string(row) + ": " + e.what());
    }

    const sim_time_t time = convert_epoch<sim_time_t>(parsed_time);
    if (!changes.empty() && time <= changes.back().time) {
      throw std::invalid_argument(
          "Capacity schedule times must be strictly increasing (row " +
          std::to_string(row) + ")");
    }
    if (parsed_nodes > configured_maximum) {
      throw std::invalid_argument(
          "Capacity schedule row " + std::to_string(row) + " requests " +
          std::to_string(parsed_nodes) + " nodes, exceeding --total_nodes=" +
          std::to_string(configured_maximum));
    }
    changes.push_back({time, static_cast<num_nodes_t>(parsed_nodes)});
  }

  if (changes.empty()) {
    throw std::invalid_argument("Capacity schedule contains no data rows: " +
                                filename);
  }
  return changes;
}

} // namespace dr_evt
