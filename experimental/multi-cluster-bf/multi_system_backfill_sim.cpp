/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file multi_system_backfill_sim.cpp
 * @brief Driver for the multi-system FCFS scheduler with spatial backfilling.
 * @details
 * Accepts every simulator option plus the systems table and the two spatial
 * policies, injects MultiSystemBackfillScheduler into the core Simulation,
 * writes the usual job and resource traces, and adds a placement sidecar and
 * a per-system summary.
 */

#include "multi_system_backfill_scheduler.hpp"
#include "params/sim_params.hpp"
#include "sim/sim.hpp"
#include "trace/epoch.hpp"
#include "trace/node_release_delay.hpp"
#include "utils/timer.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using dr_evt::experimental::MultiSystemBackfillScheduler;
using dr_evt::experimental::PlacementPolicy;
using dr_evt::experimental::SpatialBackfillPolicy;
using dr_evt::experimental::System_Spec;

/** @brief Options handled by this driver rather than by Sim_Params. */
struct Driver_Options {
  std::vector<System_Spec> systems; ///< Systems table.
  SpatialBackfillPolicy spatial_policy =
      SpatialBackfillPolicy::TARGET_ONLY; ///< Window rule.
  PlacementPolicy placement_policy =
      PlacementPolicy::BEST_FIT; ///< System choice rule.
  std::string placement_output;  ///< Sidecar path; derived when empty.
};

/** @brief Print the driver-specific options.
 * @param[out] os Stream to write to. */
void print_driver_usage(std::ostream &os) {
  os << "Multi-system options (in addition to every simulator option):\n"
        "  --systems NAME:NODES[,NAME:NODES...]\n"
        "        Inline systems table.\n"
        "  --systems_file PATH\n"
        "        Systems table with lines 'name,nodes[,...]'; '#' lines are\n"
        "        ignored (the multi-cluster machines.csv format).\n"
        "  --spatial_backfill {target_only|every_system|global_shadow}\n"
        "        Window rule for backfill candidates (default: target_only).\n"
        "  --placement_policy {best_fit|first_fit|worst_fit}\n"
        "        Choice among systems that admit a job (default: best_fit).\n"
        "  --placement_output PATH\n"
        "        Sidecar CSV 'job_id,system' (default: "
        "<outfile>_placements.csv).\n"
        "--total_nodes is set to the sum of the systems; --capacity_schedule,\n"
        "--sim_start_time, --infile_list, --checkpoint_file, and priority\n"
        "policies other than fcfs are not supported.\n\n";
}

/** @brief Fetch the value of an option given as `--opt value` or `--opt=value`.
 * @param[in] args Full argument list.
 * @param[in,out] index Position of the option; advanced past a separate value.
 * @param[in] name Option name without the value.
 * @param[in] inline_value Text after '=' when the option carried one.
 * @param[in] has_inline_value True when inline_value holds the option's value.
 * @return The option value.
 * @throws std::invalid_argument when the value is missing. */
std::string option_value(const std::vector<std::string> &args, size_t &index,
                         const std::string &name,
                         const std::string &inline_value,
                         bool has_inline_value) {
  if (has_inline_value) {
    return inline_value;
  }
  if (index + 1 >= args.size()) {
    throw std::invalid_argument(name + " requires a value");
  }
  return args[++index];
}

/** @brief Split driver options from the arguments forwarded to Sim_Params.
 * @param[in] argc Argument count.
 * @param[in] argv Argument vector.
 * @param[out] options Parsed driver options.
 * @return Arguments to forward, starting with the program name.
 * @throws std::invalid_argument on a malformed driver option. */
std::vector<std::string> split_arguments(int argc, char **argv,
                                         Driver_Options &options) {
  std::vector<std::string> args(argv, argv + argc);
  std::vector<std::string> forwarded;
  forwarded.push_back(args.empty() ? std::string("multi_system_backfill_sim")
                                   : args[0]);
  bool systems_given = false;
  for (size_t i = 1; i < args.size(); ++i) {
    std::string name = args[i];
    std::string inline_value;
    bool has_inline_value = false;
    const auto equals = name.find('=');
    if (name.rfind("--", 0) == 0 && equals != std::string::npos) {
      inline_value = name.substr(equals + 1);
      name = name.substr(0, equals);
      has_inline_value = true;
    }
    if (name == "--systems") {
      options.systems = dr_evt::experimental::parse_systems_inline(
          option_value(args, i, name, inline_value, has_inline_value));
      systems_given = true;
    } else if (name == "--systems_file") {
      options.systems = dr_evt::experimental::load_systems_file(
          option_value(args, i, name, inline_value, has_inline_value));
      systems_given = true;
    } else if (name == "--spatial_backfill") {
      options.spatial_policy =
          dr_evt::experimental::parse_spatial_backfill_policy(
              option_value(args, i, name, inline_value, has_inline_value));
    } else if (name == "--placement_policy") {
      options.placement_policy = dr_evt::experimental::parse_placement_policy(
          option_value(args, i, name, inline_value, has_inline_value));
    } else if (name == "--placement_output") {
      options.placement_output =
          option_value(args, i, name, inline_value, has_inline_value);
    } else {
      if (name == "-h" || name == "--help") {
        print_driver_usage(std::cout);
      }
      forwarded.push_back(args[i]);
    }
  }
  if (!systems_given) {
    throw std::invalid_argument("--systems or --systems_file is required");
  }
  dr_evt::experimental::validate_systems(options.systems);
  return forwarded;
}

/** @brief Reject simulator settings this experiment does not support.
 * @param[in] cfg Parsed simulator configuration.
 * @param[in] total_capacity Sum of the systems' capacities.
 * @throws std::invalid_argument when an unsupported setting is present. */
void check_supported(const dr_evt::Sim_Params &cfg,
                     dr_evt::num_nodes_t total_capacity) {
  if (cfg.m_is_nodes_set && cfg.m_total_nodes != total_capacity) {
    throw std::invalid_argument(
        "--total_nodes (" + std::to_string(cfg.m_total_nodes) +
        ") must equal the sum of the systems (" +
        std::to_string(total_capacity) + ") or be omitted");
  }
  if (!cfg.m_capacity_schedule.empty()) {
    throw std::invalid_argument(
        "--capacity_schedule is not supported with multiple systems");
  }
  if (cfg.m_sim_start_time != 0.0) {
    throw std::invalid_argument(
        "--sim_start_time (warm start) is not supported with multiple systems");
  }
  if (!cfg.m_infile_list.empty()) {
    throw std::invalid_argument(
        "--infile_list is not supported with multiple systems");
  }
  if (!cfg.m_checkpoint_file.empty()) {
    throw std::invalid_argument(
        "checkpoints are not supported with multiple systems");
  }
  if (cfg.m_priority_policy != dr_evt::PriorityPolicy::FCFS) {
    throw std::invalid_argument(
        "only --priority_policy fcfs is supported with multiple systems");
  }
  if (cfg.m_dispatch_interval != 0.0 || cfg.m_backfill_interval != 0.0) {
    throw std::invalid_argument(
        "--dispatch_interval and --backfill_interval are not supported with "
        "multiple systems; scheduling is event driven");
  }
  if (cfg.m_dispatch_on_events || cfg.m_dispatch_queue_depth != 0) {
    throw std::invalid_argument(
        "--dispatch_on_events and --dispatch_queue_depth are not supported "
        "with multiple systems");
  }
  if (cfg.m_per_pass_accounting || cfg.m_diagnostic_no_reservations) {
    throw std::invalid_argument(
        "--per_pass_accounting and --diagnostic_no_reservations are not "
        "supported with multiple systems");
  }
  if (cfg.m_backfill_queue_depth != 0) {
    throw std::invalid_argument(
        "--backfill_queue_depth is not supported with multiple systems");
  }
  if (cfg.m_planning_time_resolution != 0.0 ||
      cfg.m_planning_horizon != std::numeric_limits<dr_evt::tdiff_t>::max()) {
    throw std::invalid_argument(
        "--planning_time_resolution and --planning_horizon are not supported "
        "with multiple systems");
  }
  if (dr_evt::node_release_delay_requested(cfg.m_node_release_delay,
                                          cfg.m_node_release_delay_model,
                                          cfg.m_node_release_delay_spread)) {
    throw std::invalid_argument(
        "--node_release_delay and uniform --node_release_delay_spread are not "
        "supported with multiple systems");
  }
  if (cfg.m_backfill_policy == dr_evt::BackfillPolicy::HYBRID) {
    throw std::invalid_argument(
        "--backfill_policy hybrid is not supported with multiple systems; "
        "use easy with --spatial_backfill to select spatial protection");
  }
  if (cfg.m_backfill_policy == dr_evt::BackfillPolicy::CONSERVATIVE) {
    throw std::invalid_argument(
        "--backfill_policy conservative (temporal) is not supported; use "
        "--spatial_backfill every_system for the spatial conservative rule");
  }
}

/** @brief Derive a sibling output path by replacing or appending a suffix.
 * @param[in] outfile Main output path.
 * @param[in] suffix Text to insert before `.csv` or append.
 * @return Derived path. */
std::string derive_path(const std::string &outfile, const std::string &suffix) {
  const size_t pos = outfile.rfind(".csv");
  if (pos != std::string::npos && pos + 4 == outfile.size()) {
    return outfile.substr(0, pos) + suffix + ".csv";
  }
  return outfile + suffix + ".csv";
}

/** @brief Write the placement sidecar and print the per-system summary.
 * @param[in] sim Finished simulation.
 * @param[in] scheduler The injected scheduler, still owned by sim.
 * @param[in] placement_output Sidecar path; empty disables the file.
 * @param[out] os Stream for the summary. */
template <typename SimulationType>
void report_placements(const SimulationType &sim,
                       const MultiSystemBackfillScheduler &scheduler,
                       const std::string &placement_output, std::ostream &os) {
  const auto &trace = sim.get_trace();
  const auto &model = scheduler.model();
  const size_t num_systems = model.num_systems();
  const size_t reclaimed = trace.num_reclaimed();
  if (reclaimed != 0) {
    std::cerr << "warning: " << reclaimed
              << " job records were flushed during the run; the placement "
                 "sidecar and per-system summary cover retained jobs only\n";
  }

  std::ofstream sidecar;
  if (!placement_output.empty()) {
    sidecar.open(placement_output);
    if (!sidecar) {
      throw std::runtime_error("cannot write placement sidecar '" +
                               placement_output + "'");
    }
    sidecar << "job_id,system\n";
  }

  std::vector<size_t> jobs(num_systems, 0u);
  std::vector<double> node_seconds(num_systems, 0.0);
  double first_start = std::numeric_limits<double>::infinity();
  double last_end = -std::numeric_limits<double>::infinity();
  dr_evt::job_no_t job_no = static_cast<dr_evt::job_no_t>(reclaimed);
  for (const auto &job : trace.data()) {
    const dr_evt::job_no_t id = job_no++;
    if (!job.is_scheduled()) {
      continue;
    }
    const auto placed = scheduler.placements().find(id);
    if (placed == scheduler.placements().end()) {
      throw std::logic_error("scheduled job " + std::to_string(id) +
                             " has no recorded placement");
    }
    const auto k = placed->second.system;
    const double begin = dr_evt::convert_epoch<double>(job.get_begin_time());
    const double end = dr_evt::convert_epoch<double>(job.get_end_time());
    jobs[k] += 1;
    node_seconds[k] += static_cast<double>(job.get_num_nodes()) * (end - begin);
    first_start = std::min(first_start, begin);
    last_end = std::max(last_end, end);
    if (sidecar.is_open()) {
      sidecar << id << "," << model.system(k).name << "\n";
    }
  }

  const double span = last_end > first_start ? last_end - first_start : 0.0;
  os << "\n=== Per-system summary ===\n";
  os << "Spatial backfill: "
     << dr_evt::experimental::to_string(scheduler.spatial_policy())
     << ", placement: "
     << dr_evt::experimental::to_string(scheduler.placement_policy()) << "\n";
  for (size_t k = 0; k < num_systems; ++k) {
    const auto &system = model.system(static_cast<unsigned>(k));
    const double utilization =
        span > 0.0 ? node_seconds[k] / (span * system.capacity) : 0.0;
    os << system.name << ": capacity=" << system.capacity << " jobs=" << jobs[k]
       << " node_seconds=" << std::fixed << std::setprecision(0)
       << node_seconds[k] << " utilization=" << std::setprecision(4)
       << utilization << "\n";
  }
  if (!placement_output.empty()) {
    os << "Placements written to: " << placement_output << "\n";
  }
}

/** @brief Build the scheduler, run the simulation, and write all outputs.
 * @param[in] cfg Simulator configuration with total_nodes already set.
 * @param[in] options Driver options. */
template <typename SimulationType>
void run_simulation(const dr_evt::Sim_Params &cfg,
                    const Driver_Options &options) {
  auto scheduler = std::make_unique<MultiSystemBackfillScheduler>(
      options.systems, options.spatial_policy, options.placement_policy,
      cfg.m_backfill_policy, /*initial_job_count=*/0);
  const MultiSystemBackfillScheduler *view = scheduler.get();
  SimulationType sim(cfg, std::move(scheduler));
  sim.run();
  sim.write_simulated_trace();

  std::string resource_file = cfg.get_resource_trace();
  if (resource_file.empty() && !cfg.get_outfile().empty()) {
    resource_file = derive_path(cfg.get_outfile(), "_resources");
  }
  if (!resource_file.empty()) {
    sim.write_resource_trace(resource_file);
  }

  std::cout << std::endl;
  sim.print_stats(std::cout);

  std::string placement_output = options.placement_output;
  if (placement_output.empty() && !cfg.get_outfile().empty()) {
    placement_output = derive_path(cfg.get_outfile(), "_placements");
  }
  report_placements(sim, *view, placement_output, std::cout);
}

} // namespace

/** @brief Run the multi-system scheduling simulator.
 * @param[in] argc Number of command-line arguments.
 * @param[in] argv Command-line argument vector.
 * @return `EXIT_SUCCESS` after a completed simulation, or `EXIT_FAILURE` when
 * configuration or simulation raises an exception. */
int main(int argc, char **argv) {
  Driver_Options options;
  std::vector<std::string> forwarded;
  try {
    forwarded = split_arguments(argc, argv, options);
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    print_driver_usage(std::cerr);
    return EXIT_FAILURE;
  }

  std::vector<char *> forwarded_argv;
  forwarded_argv.reserve(forwarded.size() + 1);
  for (auto &arg : forwarded) {
    forwarded_argv.push_back(arg.data());
  }
  forwarded_argv.push_back(nullptr);
  int forwarded_argc = static_cast<int>(forwarded.size());
  char **forwarded_ptr = forwarded_argv.data();

  dr_evt::Sim_Params cfg;
  cfg.getopt(forwarded_argc, forwarded_ptr);

  dr_evt::num_nodes_t total_capacity = 0;
  for (const auto &system : options.systems) {
    total_capacity += system.capacity;
  }

  int rc = EXIT_SUCCESS;
  try {
    check_supported(cfg, total_capacity);
    cfg.m_total_nodes = total_capacity;
    cfg.print();
    std::cout << "Systems:";
    for (const auto &system : options.systems) {
      std::cout << " " << system.name << ":" << system.capacity;
    }
    std::cout << "\nSpatial backfill: "
              << dr_evt::experimental::to_string(options.spatial_policy)
              << "\nPlacement policy: "
              << dr_evt::experimental::to_string(options.placement_policy)
              << std::endl;

    const double t_start = dr_evt::get_time();
    if (cfg.m_trace_type == dr_evt::TraceType::PCON) {
      run_simulation<dr_evt::PconSimulation>(cfg, options);
    } else if (cfg.m_trace_type == dr_evt::TraceType::SWF) {
      run_simulation<dr_evt::SwfSimulation>(cfg, options);
    } else {
      run_simulation<dr_evt::Simulation>(cfg, options);
    }
    std::cout << "\nWall clock time to run simulation: "
              << dr_evt::get_time() - t_start << " (sec)" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    rc = EXIT_FAILURE;
  }
  return rc;
}
