/******************************************************************************
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * Native MPI version of grpc_performance_dispatch.py.
 *
 * Rank zero reads arrivals and selects a system.  Every other rank owns one
 * independent DR_EVT Simulation.  Only small command/result objects cross
 * MPI; Ser20 never serializes Simulation itself.
 */

#include "dr_evt_config.hpp"
#include "sim/sim.hpp"
#include "utils/state_io_ser20.hpp"

#define OMPI_SKIP_MPICXX 1
#define MPICH_SKIP_MPICXX 1
#include <mpi.h>

#include <ser20/types/string.hpp>
#include <ser20/types/vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

#if DR_EVT_LEGACY_QUEUE_INPUT
constexpr const char *kDefaultQueue = "pbatch";
constexpr const char *kQueueField = "queue";
#else
constexpr const char *kDefaultQueue = "1";
constexpr const char *kQueueField = "q_id";
#endif

constexpr int kSizeTag = 100;
constexpr int kPayloadTag = 101;

enum class Operation : std::uint8_t { Snapshot, Append, Finish };
enum class DispatchPolicy : std::uint8_t {
  Turnaround,
  RelPerfOnly,
  WaitTimeOnly
};
enum class WallTimePolicy : std::uint8_t {
  AdaptedLimit,
  ActualDuration
};

struct JobMessage {
  double submit_time = 0.0;
  std::uint32_t num_nodes = 0;
  std::string queue;
  double duration = 0.0;
  double limit_time = 0.0;

  template <class Archive> void serialize(Archive &archive) {
    archive(submit_time, num_nodes, queue, duration, limit_time);
  }
};

struct Request {
  Operation operation = Operation::Snapshot;
  double target_time = 0.0;
  double prediction_utilization = 1.0;
  JobMessage job;

  template <class Archive> void serialize(Archive &archive) {
    archive(operation, target_time, prediction_utilization, job);
  }
};

struct ReleaseMessage {
  double time = 0.0;
  std::uint32_t nodes_released = 0;

  template <class Archive> void serialize(Archive &archive) {
    archive(time, nodes_released);
  }
};

struct WindowMessage {
  double current_time = 0.0;
  std::uint32_t available_nodes = 0;
  double shadow_time = -1.0;
  std::vector<ReleaseMessage> releases;

  template <class Archive> void serialize(Archive &archive) {
    archive(current_time, available_nodes, shadow_time, releases);
  }
};

struct StatisticsMessage {
  std::uint64_t jobs_submitted = 0;
  std::uint64_t jobs_completed = 0;
  double avg_turnaround_time = 0.0;
  double avg_bounded_slowdown = 0.0;
  double makespan = 0.0;

  template <class Archive> void serialize(Archive &archive) {
    archive(jobs_submitted, jobs_completed, avg_turnaround_time,
            avg_bounded_slowdown, makespan);
  }
};

struct Response {
  bool ok = true;
  std::string error;
  WindowMessage window;
  double prediction_horizon = 0.0;
  std::uint64_t job_idx = 0;
  StatisticsMessage statistics;

  template <class Archive> void serialize(Archive &archive) {
    archive(ok, error, window, prediction_horizon, job_idx, statistics);
  }
};

template <typename T> void send_serialized(const T &value, int rank) {
  std::vector<char> bytes;
  const auto serialized_size = dr_evt::serialize_binary(value, bytes);
  if (serialized_size != bytes.size())
    throw std::runtime_error("Ser20 reported an inconsistent message size");
  const auto size = static_cast<std::uint64_t>(bytes.size());
  if (size > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    throw std::length_error("serialized MPI message exceeds INT_MAX bytes");
  }
  MPI_Send(&size, 1, MPI_UINT64_T, rank, kSizeTag, MPI_COMM_WORLD);
  MPI_Send(bytes.data(), static_cast<int>(bytes.size()), MPI_BYTE, rank,
           kPayloadTag, MPI_COMM_WORLD);
}

template <typename T> T receive_serialized(int rank) {
  std::uint64_t size = 0;
  MPI_Recv(&size, 1, MPI_UINT64_T, rank, kSizeTag, MPI_COMM_WORLD,
           MPI_STATUS_IGNORE);
  if (size > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    throw std::length_error("serialized MPI message exceeds INT_MAX bytes");
  }
  std::vector<char> bytes(static_cast<std::size_t>(size));
  MPI_Recv(bytes.data(), static_cast<int>(bytes.size()), MPI_BYTE, rank,
           kPayloadTag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  T value;
  dr_evt::deserialize_binary(value, bytes);
  return value;
}

struct Options {
  std::string jobs;
  std::string ground_truth;
  std::string prediction;
  std::string applications;
  std::string systems_table;
  std::optional<std::string> output;
  std::uint64_t seed = 0;
  double prediction_utilization = 1.0;
  double max_time_limit = std::numeric_limits<double>::infinity();
  DispatchPolicy dispatch_policy = DispatchPolicy::Turnaround;
  WallTimePolicy wall_time_policy = WallTimePolicy::AdaptedLimit;
};

[[noreturn]] void usage(const char *program, const std::string &error = {}) {
  if (!error.empty())
    std::cerr << "error: " << error << "\n\n";
  std::cerr << "Usage: mpirun -np <systems+1> " << program << " OPTIONS\n"
            << "  --jobs PATH\n"
            << "  --ground-truth PATH\n"
            << "  --prediction PATH\n"
            << "  --applications PATH       app-to-requirement CSV\n"
            << "  --systems PATH\n"
            << "  --seed INTEGER            sampling seed (default: 0)\n"
            << "  --prediction-utilization U  value in [0,1] (default: 1)\n"
            << "  --max-time-limit SECONDS  maximum submitted wall time "
               "(default: unlimited)\n"
            << "  --dispatch-policy POLICY  turnaround, RelPerfOnly, or "
               "WaitTimeOnly "
               "(default: turnaround)\n"
            << "  --wall-time-policy POLICY adapted-limit or "
               "actual-duration (default: adapted-limit)\n"
            << "  --output PATH             decision CSV (default: stdout)\n";
  throw std::invalid_argument(error.empty() ? "help requested" : error);
}

std::string option_value(int &index, int argc, char **argv,
                         const std::string &name) {
  if (++index >= argc)
    usage(argv[0], name + " requires a value");
  return argv[index];
}

Options parse_options(int argc, char **argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--jobs")
      options.jobs = option_value(i, argc, argv, arg);
    else if (arg == "--ground-truth")
      options.ground_truth = option_value(i, argc, argv, arg);
    else if (arg == "--prediction")
      options.prediction = option_value(i, argc, argv, arg);
    else if (arg == "--applications")
      options.applications = option_value(i, argc, argv, arg);
    else if (arg == "--systems")
      options.systems_table = option_value(i, argc, argv, arg);
    else if (arg == "--seed")
      options.seed = std::stoull(option_value(i, argc, argv, arg));
    else if (arg == "--prediction-utilization")
      options.prediction_utilization =
          std::stod(option_value(i, argc, argv, arg));
    else if (arg == "--max-time-limit")
      options.max_time_limit = std::stod(option_value(i, argc, argv, arg));
    else if (arg == "--dispatch-policy") {
      const auto value = option_value(i, argc, argv, arg);
      if (value == "turnaround")
        options.dispatch_policy = DispatchPolicy::Turnaround;
      else if (value == "RelPerfOnly")
        options.dispatch_policy = DispatchPolicy::RelPerfOnly;
      else if (value == "WaitTimeOnly")
        options.dispatch_policy = DispatchPolicy::WaitTimeOnly;
      else
        usage(argv[0], "--dispatch-policy must be turnaround, RelPerfOnly, "
                       "or WaitTimeOnly");
    } else if (arg == "--wall-time-policy") {
      const auto value = option_value(i, argc, argv, arg);
      if (value == "adapted-limit")
        options.wall_time_policy = WallTimePolicy::AdaptedLimit;
      else if (value == "actual-duration")
        options.wall_time_policy = WallTimePolicy::ActualDuration;
      else
        usage(argv[0], "--wall-time-policy must be adapted-limit or "
                       "actual-duration");
    }
    else if (arg == "--output")
      options.output = option_value(i, argc, argv, arg);
    else if (arg == "--help" || arg == "-h")
      usage(argv[0]);
    else
      usage(argv[0], "unknown option: " + arg);
  }
  if (options.jobs.empty() || options.ground_truth.empty() ||
      options.prediction.empty() ||
      options.applications.empty() || options.systems_table.empty())
    usage(argv[0], "--jobs, --ground-truth, --prediction, --applications, "
                   "and --systems are required");
  if (!std::isfinite(options.prediction_utilization) ||
      options.prediction_utilization < 0.0 ||
      options.prediction_utilization > 1.0)
    usage(argv[0], "--prediction-utilization must be in [0,1]");
  if (std::isnan(options.max_time_limit) || options.max_time_limit <= 0.0)
    usage(argv[0], "--max-time-limit must be positive");
  return options;
}

std::vector<std::string> split_csv(const std::string &line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
        field.push_back('"');
        ++i;
      } else {
        quoted = !quoted;
      }
    } else if (c == ',' && !quoted) {
      fields.push_back(field);
      field.clear();
    } else {
      field.push_back(c);
    }
  }
  if (quoted)
    throw std::runtime_error("unterminated quoted CSV field");
  if (!field.empty() && field.back() == '\r')
    field.pop_back();
  fields.push_back(field);
  return fields;
}

using Header = std::map<std::string, std::size_t>;

Header make_header(const std::vector<std::string> &fields) {
  Header header;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    std::string name = fields[i];
    if (i == 0 && !name.empty() && name.front() == '#')
      name.erase(name.begin());
    header.emplace(std::move(name), i);
  }
  return header;
}

const std::string &field(const std::vector<std::string> &row,
                         const Header &header, const std::string &name) {
  const auto found = header.find(name);
  if (found == header.end() || found->second >= row.size())
    throw std::runtime_error("missing CSV field: " + name);
  return row[found->second];
}

std::string optional_field(const std::vector<std::string> &row,
                           const Header &header, const std::string &name,
                           std::string fallback) {
  const auto found = header.find(name);
  if (found == header.end() || found->second >= row.size() ||
      row[found->second].empty())
    return fallback;
  return row[found->second];
}

struct Job {
  std::string id;
  double submit_time;
  std::uint32_t num_nodes;
  std::string queue;
  double duration;
  double limit_time;
};

/** Read a validated job stream.
 * @param[in] path CSV path supplied by the caller.
 * @return Jobs in nondecreasing submission order.
 * @throws std::runtime_error if the input is missing or invalid.
 */
std::vector<Job> read_jobs(const std::string &path) {
  std::ifstream stream(path);
  if (!stream)
    throw std::runtime_error("cannot open jobs file: " + path);
  std::string line;
  if (!std::getline(stream, line))
    throw std::runtime_error("jobs file is empty: " + path);
  const Header header = make_header(split_csv(line));
  const std::string submit_field =
      header.contains("submit_time") ? "submit_time" : "job_submit_time";
  for (const auto &name : {submit_field, std::string("time_limit"),
                           std::string("num_nodes")})
    if (!header.contains(name))
      throw std::runtime_error(path + " must contain " + name);
  const std::string duration_field =
      header.contains("actual_run_time") ? "actual_run_time" : "duration";
  if (!header.contains(duration_field))
    throw std::runtime_error(path +
                             " must contain actual_run_time or duration");

  std::vector<Job> jobs;
  while (std::getline(stream, line)) {
    if (line.empty())
      continue;
    const auto row = split_csv(line);
    Job job{
        optional_field(row, header, "job_id", std::to_string(jobs.size())),
        std::stod(field(row, header, submit_field)),
        static_cast<std::uint32_t>(std::stoul(field(row, header, "num_nodes"))),
        optional_field(row, header, kQueueField, kDefaultQueue),
        std::stod(field(row, header, duration_field)),
        std::stod(field(row, header, "time_limit"))};
    if (job.num_nodes == 0 || !std::isfinite(job.submit_time) ||
        !std::isfinite(job.duration) || !std::isfinite(job.limit_time) ||
        job.duration <= 0.0 || job.limit_time <= 0.0 ||
        job.duration > job.limit_time)
      throw std::runtime_error(path + ": job " + job.id +
                               " has invalid size or duration");
    if (!jobs.empty() && jobs.back().submit_time > job.submit_time)
      throw std::runtime_error(path + ": jobs must be sorted by submit_time");
    jobs.push_back(std::move(job));
  }
  return jobs;
}

enum class SystemRequirement { CpuOnly, GpuOnly, GpuPortable };

std::string to_string(SystemRequirement requirement) {
  switch (requirement) {
  case SystemRequirement::CpuOnly:
    return "CPU-only";
  case SystemRequirement::GpuOnly:
    return "GPU-only";
  case SystemRequirement::GpuPortable:
    return "GPU-portable";
  }
  throw std::logic_error("unknown system requirement");
}

SystemRequirement parse_requirement(const std::string &value) {
  if (value == "CPU-only")
    return SystemRequirement::CpuOnly;
  if (value == "GPU-only")
    return SystemRequirement::GpuOnly;
  if (value == "GPU-portable")
    return SystemRequirement::GpuPortable;
  throw std::runtime_error("invalid sys_requirement '" + value +
                           "'; expected CPU-only, GPU-only, or GPU-portable");
}

std::map<std::string, SystemRequirement>
read_application_requirements(const std::string &path) {
  std::ifstream stream(path);
  if (!stream)
    throw std::runtime_error("cannot open applications file: " + path);
  std::string line;
  if (!std::getline(stream, line))
    throw std::runtime_error("applications file is empty: " + path);
  const Header header = make_header(split_csv(line));
  if (!header.contains("app") || !header.contains("sys_requirement"))
    throw std::runtime_error(path + " must contain app and sys_requirement");
  std::map<std::string, SystemRequirement> requirements;
  while (std::getline(stream, line)) {
    if (line.empty())
      continue;
    const auto row = split_csv(line);
    const std::string app = field(row, header, "app");
    if (app.empty())
      throw std::runtime_error(path + ": app must not be empty");
    if (!requirements
             .emplace(app,
                      parse_requirement(field(row, header, "sys_requirement")))
             .second)
      throw std::runtime_error(path + ": duplicate app " + app);
  }
  if (requirements.empty())
    throw std::runtime_error("applications file is empty: " + path);
  return requirements;
}

struct System {
  std::string id;
  std::uint32_t physical_nodes;
  bool gpu_enabled;
  std::string cpu_performance_column;
  std::string gpu_performance_column;
};

std::vector<System> read_systems(const std::string &path) {
  std::ifstream stream(path);
  if (!stream)
    throw std::runtime_error("cannot open systems file: " + path);
  std::string line;
  if (!std::getline(stream, line))
    throw std::runtime_error("systems file is empty: " + path);

  const Header header = make_header(split_csv(line));
  for (const auto *name : {"machine", "size", "GPU"})
    if (!header.contains(name))
      throw std::runtime_error(path + " must contain " + name);

  std::vector<System> systems;
  std::set<std::string> ids;
  while (std::getline(stream, line)) {
    if (line.empty())
      continue;
    const auto row = split_csv(line);
    const std::string id = field(row, header, "machine");
    const std::string gpu = field(row, header, "GPU");
    const bool gpu_enabled = gpu == "GPU-enabled";
    if (!gpu_enabled && gpu != "CPU-only")
      throw std::runtime_error(path + ": invalid GPU value " + gpu);
    System system{
        id, static_cast<std::uint32_t>(std::stoul(field(row, header, "size"))),
        gpu_enabled, gpu_enabled ? id + "-cpu" : id,
        gpu_enabled ? id + "-gpu" : ""};
    if (system.id.empty() || system.physical_nodes == 0)
      throw std::runtime_error(path + ": invalid system row");
    if (!ids.insert(system.id).second)
      throw std::runtime_error(path + ": duplicate system_id " + system.id);
    systems.push_back(std::move(system));
  }
  if (systems.empty())
    throw std::runtime_error("systems file is empty: " + path);
  return systems;
}

struct PerformancePair {
  double ground_truth;
  std::optional<double> predicted;
};

struct SystemPerformance {
  std::optional<PerformancePair> cpu;
  std::optional<PerformancePair> gpu;
};

struct Workload {
  std::string app;
  std::string args;
  std::uint32_t ranks;
  SystemRequirement requirement;
  std::vector<SystemPerformance> performance;
};

struct WorkloadCatalog {
  std::vector<std::string> apps;
  std::vector<std::vector<Workload>> samples_by_app;
};

std::optional<double> read_performance(const std::vector<std::string> &row,
                                       const Header &header,
                                       const std::string &column,
                                       const std::string &context) {
  if (column.empty())
    return std::nullopt;
  const std::string &text = field(row, header, column);
  if (text.empty())
    return std::nullopt;
  const double value = std::stod(text);
  if (!std::isfinite(value) || value <= 0.0)
    throw std::runtime_error(context + ": " + column +
                             " must be empty or positive");
  return value;
}

/** Read ground truth and an optional prediction for one execution mode.
 * @param[in] row Ground-truth row.
 * @param[in] header Ground-truth header lookup.
 * @param[in] prediction_row Prediction row with the same workload identity, or
 * null when that workload has no prediction row.
 * @param[in] prediction_header Prediction header lookup.
 * @param[in] column Execution-mode column name.
 * @param[in] context Diagnostic context.
 * @return Ground truth with its optional prediction, or no value when ground
 * truth is unavailable.
 * @throws std::runtime_error if a present value is invalid.
 */
std::optional<PerformancePair>
read_performance_pair(const std::vector<std::string> &row,
                      const Header &header,
                      const std::vector<std::string> *prediction_row,
                      const Header &prediction_header,
                      const std::string &column,
                      const std::string &context) {
  if (column.empty())
    return std::nullopt;
  const auto ground_truth = read_performance(row, header, column, context);
  if (!ground_truth)
    return std::nullopt;
  const auto predicted = prediction_row == nullptr
                             ? std::nullopt
                             : read_performance(*prediction_row,
                                                prediction_header, column,
                                                context);
  return PerformancePair{*ground_truth, predicted};
}

/** Join ground truth and predictions by workload identity.
 * @param[in] ground_truth_path Measured-speedup CSV.
 * @param[in] prediction_path Predicted-speedup CSV.
 * @param[in] systems Configured systems and their execution-mode columns.
 * @param[in] requirements Application allowlist and compatibility requirements;
 * performance rows for absent applications are ignored.
 * @return Valid runnable workloads grouped by application.
 * @throws std::runtime_error if either table is malformed or identities differ.
 */
WorkloadCatalog read_workloads(
    const std::string &ground_truth_path, const std::string &prediction_path,
    const std::vector<System> &systems,
    const std::map<std::string, SystemRequirement> &requirements) {
  std::ifstream stream(ground_truth_path);
  std::ifstream prediction_stream(prediction_path);
  if (!stream || !prediction_stream)
    throw std::runtime_error("cannot open ground-truth or prediction table");
  std::string line, prediction_line;
  if (!std::getline(stream, line) ||
      !std::getline(prediction_stream, prediction_line))
    throw std::runtime_error("ground-truth or prediction table is empty");
  const Header header = make_header(split_csv(line));
  const Header prediction_header = make_header(split_csv(prediction_line));
  const auto identity_name = [](const Header &value, const char *upper,
                                const char *lower) -> std::string {
    if (value.contains(upper))
      return upper;
    if (value.contains(lower))
      return lower;
    throw std::runtime_error(std::string("performance table must contain ") +
                             upper);
  };
  const std::array<std::string, 3> identity{
      identity_name(header, "App", "app"), identity_name(header, "Args", "args"),
      identity_name(header, "Ranks", "ranks")};
  const std::array<std::string, 3> predicted_identity{
      identity_name(prediction_header, "App", "app"),
      identity_name(prediction_header, "Args", "args"),
      identity_name(prediction_header, "Ranks", "ranks")};
  for (const auto &system : systems)
    for (const auto &column : {system.cpu_performance_column,
                               system.gpu_performance_column}) {
      if (column.empty())
        continue;
      if (!header.contains(column))
        throw std::runtime_error(ground_truth_path + " must contain " + column);
      if (!prediction_header.contains(column))
        throw std::runtime_error(prediction_path + " must contain " + column);
    }

  using Identity = std::tuple<std::string, std::string, std::uint32_t>;
  std::map<Identity, std::vector<std::string>> predictions;
  while (std::getline(prediction_stream, prediction_line)) {
    if (prediction_line.empty())
      continue;
    auto row = split_csv(prediction_line);
    Identity key{
        field(row, prediction_header, predicted_identity[0]),
        field(row, prediction_header, predicted_identity[1]),
        static_cast<std::uint32_t>(
            std::stoul(field(row, prediction_header, predicted_identity[2])))};
    if (!requirements.contains(std::get<0>(key)))
      continue;
    if (!predictions.emplace(std::move(key), std::move(row)).second)
      throw std::runtime_error(prediction_path +
                               ": duplicate workload identity");
  }

  WorkloadCatalog catalog;
  std::map<std::string, std::size_t> app_indices;
  std::set<Identity> identities;
  std::size_t row_number = 1;
  std::size_t unavailable_rows = 0;
  while (std::getline(stream, line)) {
    ++row_number;
    if (line.empty())
      continue;
    const auto row = split_csv(line);
    const std::string app = field(row, header, identity[0]);
    const Identity workload_identity{
        app, field(row, header, identity[1]),
        static_cast<std::uint32_t>(std::stoul(field(row, header, identity[2])))};
    const auto requirement = requirements.find(app);
    if (requirement == requirements.end())
      continue;
    if (!identities.insert(workload_identity).second)
      throw std::runtime_error(ground_truth_path +
                               ": duplicate workload at row " +
                               std::to_string(row_number));
    const auto prediction = predictions.find(workload_identity);
    const auto *prediction_row = prediction == predictions.end()
                                     ? nullptr
                                     : &prediction->second;
    Workload workload{
        app, field(row, header, identity[1]),
        static_cast<std::uint32_t>(std::stoul(field(row, header, identity[2]))),
        requirement->second, {}};
    if (workload.app.empty() || workload.ranks == 0)
      throw std::runtime_error(ground_truth_path +
                               ": invalid workload at row " +
                               std::to_string(row_number));
    bool runnable = false;
    for (const auto &system : systems) {
      const auto context =
          ground_truth_path + ": row " + std::to_string(row_number);
      SystemPerformance performance{
          read_performance_pair(row, header, prediction_row, prediction_header,
                                system.cpu_performance_column, context),
          system.gpu_enabled
              ? read_performance_pair(
                    row, header, prediction_row, prediction_header,
                    system.gpu_performance_column, context)
              : std::nullopt};
      if (workload.requirement == SystemRequirement::CpuOnly)
        runnable = runnable || performance.cpu.has_value();
      else if (workload.requirement == SystemRequirement::GpuOnly)
        runnable = runnable || performance.gpu.has_value();
      else
        runnable = runnable || performance.cpu.has_value() ||
                   performance.gpu.has_value();
      workload.performance.push_back(performance);
    }
    if (prediction != predictions.end())
      predictions.erase(prediction);
    if (!runnable) {
      ++unavailable_rows;
      continue;
    }
    auto [app_it, new_app] =
        app_indices.emplace(workload.app, catalog.apps.size());
    if (new_app) {
      catalog.apps.push_back(workload.app);
      catalog.samples_by_app.emplace_back();
    }
    catalog.samples_by_app[app_it->second].push_back(std::move(workload));
  }
  if (!predictions.empty())
    throw std::runtime_error(
        "prediction table contains workloads absent from ground truth");
  if (catalog.apps.empty())
    throw std::runtime_error("performance tables have no runnable rows");
  if (unavailable_rows != 0)
    std::cerr << "Skipped " << unavailable_rows
              << " workload rows with no measurement for a compatible "
                 "configured system\n";
  return catalog;
}

const Workload &sample_workload(const WorkloadCatalog &catalog,
                                std::mt19937_64 &generator) {
  std::uniform_int_distribution<std::size_t> app_distribution(
      0, catalog.apps.size() - 1);
  const auto app_index = app_distribution(generator);
  const auto &samples = catalog.samples_by_app[app_index];
  std::uniform_int_distribution<std::size_t> sample_distribution(
      0, samples.size() - 1);
  return samples[sample_distribution(generator)];
}

double estimate_release_wait(const WindowMessage &window,
                             std::uint32_t required_nodes) {
  auto available = window.available_nodes;
  if (available >= required_nodes)
    return 0.0;
  for (const auto &release : window.releases) {
    available += release.nodes_released;
    if (available >= required_nodes)
      return std::max(0.0, release.time - window.current_time);
  }
  return std::numeric_limits<double>::infinity();
}

double estimate_wait(const WindowMessage &window, std::uint32_t required_nodes,
                     double runtime, double prediction_horizon) {
  const bool has_waiting_head = window.shadow_time > window.current_time;
  const bool can_start_now = window.available_nodes >= required_nodes;
  const bool can_backfill_now =
      can_start_now &&
      (!has_waiting_head || window.current_time + runtime <= window.shadow_time);
  if (can_backfill_now)
    return 0.0;
  if (!has_waiting_head)
    return estimate_release_wait(window, required_nodes);
  return window.shadow_time - window.current_time + prediction_horizon;
}

struct Choice {
  std::size_t index;
  const char *execution_mode;
  double ground_truth_relative_performance;
  double predicted_relative_performance;
  bool prediction_fallback;
  double estimated_wait;
  double estimated_duration;
  double actual_duration;
  double predicted_time_limit;
  double actual_time_limit;
  double submitted_time_limit;
  std::uint32_t time_limit_doublings;
  double predicted_turnaround;
};

/** Increase a predicted wall-time limit enough to admit the known runtime.
 *
 * This models a user correcting the request before the successful submission;
 * failed attempts consume no simulated resources.
 *
 * @param[in] predicted_limit Initial performance-scaled limit.
 * @param[in] actual_duration Ground-truth runtime on the candidate system.
 * @param[in] maximum_limit Largest permitted wall-time request.
 * @return Final capped limit and the number of doublings performed.
 */
std::pair<double, std::uint32_t>
adjust_time_limit(double predicted_limit, double actual_duration,
                  double maximum_limit) {
  double limit = std::min(predicted_limit, maximum_limit);
  std::uint32_t doublings = 0;
  while (limit < actual_duration && limit < maximum_limit) {
    limit = std::min(limit * 2.0, maximum_limit);
    ++doublings;
  }
  return {limit, doublings};
}

/** Select a feasible system using predicted turnaround or paper Algorithm 2.
 * @param[in] job Job request with its effective node count.
 * @param[in] workload Sampled application workload.
 * @param[in] systems Configured execution systems.
 * @param[in] snapshots Current scheduler state for each system.
 * @param[in] max_time_limit Maximum permitted submitted wall-time limit.
 * @param[in] dispatch_policy System-selection policy. Wait-only dispatch does
 * not use predicted relative performance to rank systems. Turnaround and
 * RelPerfOnly fall back to wait-only selection when no feasible execution mode
 * has a prediction.
 * @param[in] wall_time_policy Submitted wall-time calculation policy.
 * @return Best compatible system whose ground-truth runtime is within the
 * maximum and, when a prediction is available, whose predicted runtime is also
 * within the maximum; returns no choice if none qualifies.
 */
std::optional<Choice>
choose_system(const Job &job, const Workload &workload,
              const std::vector<System> &systems,
              const std::vector<Response> &snapshots, double max_time_limit,
              DispatchPolicy dispatch_policy,
              WallTimePolicy wall_time_policy) {
  bool prediction_available = dispatch_policy == DispatchPolicy::WaitTimeOnly;
  for (std::size_t i = 0; !prediction_available && i < systems.size(); ++i) {
    if (job.num_nodes > systems[i].physical_nodes)
      continue;
    const auto available = [&](const auto &performance) {
      return performance && performance->predicted &&
             job.duration / performance->ground_truth <= max_time_limit;
    };
    if (workload.requirement == SystemRequirement::CpuOnly)
      prediction_available = available(workload.performance[i].cpu);
    else if (workload.requirement == SystemRequirement::GpuOnly)
      prediction_available = systems[i].gpu_enabled &&
                             available(workload.performance[i].gpu);
    else
      prediction_available = available(workload.performance[i].cpu) ||
                             (systems[i].gpu_enabled &&
                              available(workload.performance[i].gpu));
  }
  const bool prediction_fallback =
      dispatch_policy != DispatchPolicy::WaitTimeOnly && !prediction_available;
  const auto effective_policy = prediction_fallback
                                    ? DispatchPolicy::WaitTimeOnly
                                    : dispatch_policy;
  std::optional<Choice> best;
  bool best_available_now = false;
  for (std::size_t i = 0; i < systems.size(); ++i) {
    if (job.num_nodes > systems[i].physical_nodes)
      continue;
    std::optional<PerformancePair> performance;
    const char *execution_mode = "CPU";
    if (workload.requirement == SystemRequirement::CpuOnly) {
      performance = workload.performance[i].cpu;
      if (effective_policy != DispatchPolicy::WaitTimeOnly &&
          performance && !performance->predicted)
        performance = std::nullopt;
    } else if (workload.requirement == SystemRequirement::GpuOnly) {
      if (!systems[i].gpu_enabled)
        continue;
      performance = workload.performance[i].gpu;
      if (effective_policy != DispatchPolicy::WaitTimeOnly &&
          performance && !performance->predicted)
        performance = std::nullopt;
      execution_mode = "GPU";
    } else if (!systems[i].gpu_enabled) {
      performance = workload.performance[i].cpu;
      if (effective_policy != DispatchPolicy::WaitTimeOnly &&
          performance && !performance->predicted)
        performance = std::nullopt;
    } else {
      if (workload.performance[i].cpu &&
          (effective_policy == DispatchPolicy::WaitTimeOnly ||
           workload.performance[i].cpu->predicted) &&
          job.duration / workload.performance[i].cpu->ground_truth <=
              max_time_limit)
        performance = workload.performance[i].cpu;
      if (workload.performance[i].gpu &&
          (effective_policy == DispatchPolicy::WaitTimeOnly ||
           workload.performance[i].gpu->predicted) &&
          job.duration / workload.performance[i].gpu->ground_truth <=
              max_time_limit &&
          (!performance || effective_policy == DispatchPolicy::WaitTimeOnly ||
           *workload.performance[i].gpu->predicted >
               *performance->predicted)) {
        performance = workload.performance[i].gpu;
        execution_mode = "GPU";
      }
    }
    if (!performance)
      continue;
    const double predicted_speedup =
        effective_policy == DispatchPolicy::WaitTimeOnly
            ? 1.0
            : *performance->predicted;
    const double predicted_duration = job.duration / predicted_speedup;
    const double actual_duration = job.duration / performance->ground_truth;
    if ((effective_policy != DispatchPolicy::WaitTimeOnly &&
         predicted_duration > max_time_limit) ||
        actual_duration > max_time_limit)
      continue;
    const double predicted_limit = job.limit_time / predicted_speedup;
    const double actual_limit = job.limit_time / performance->ground_truth;
    auto [submitted_limit, doublings] =
        wall_time_policy == WallTimePolicy::ActualDuration
            ? std::pair{actual_duration, std::uint32_t{0}}
            : adjust_time_limit(predicted_limit, actual_duration,
                                max_time_limit);
    submitted_limit = std::ceil(submitted_limit);
    if (submitted_limit > max_time_limit)
      continue;
    const double wait =
        estimate_wait(snapshots[i].window, job.num_nodes, submitted_limit,
                      snapshots[i].prediction_horizon);
    if (effective_policy != DispatchPolicy::RelPerfOnly && !std::isfinite(wait))
      continue;
    Choice choice{i,
                  execution_mode,
                  performance->ground_truth,
                  predicted_speedup,
                  prediction_fallback,
                  wait,
                  predicted_duration,
                  actual_duration,
                  predicted_limit,
                  actual_limit,
                  submitted_limit,
                  doublings,
                  wait + predicted_duration};
    const bool available_now =
        snapshots[i].window.available_nodes >= job.num_nodes;
    const bool rel_perf_only_better =
        effective_policy == DispatchPolicy::RelPerfOnly &&
        (!best || (available_now && !best_available_now) ||
         (available_now == best_available_now &&
          (choice.predicted_relative_performance >
               best->predicted_relative_performance ||
           (choice.predicted_relative_performance ==
                best->predicted_relative_performance &&
            choice.index < best->index))));
    const bool turnaround_better =
        effective_policy == DispatchPolicy::Turnaround &&
        (!best || std::tie(choice.predicted_turnaround, choice.estimated_wait,
                          choice.index) <
                     std::tie(best->predicted_turnaround,
                              best->estimated_wait, best->index));
    const bool wait_better =
        effective_policy == DispatchPolicy::WaitTimeOnly &&
        (!best || std::tie(choice.estimated_wait, choice.index) <
                     std::tie(best->estimated_wait, best->index));
    if (rel_perf_only_better || turnaround_better || wait_better) {
      best = choice;
      best_available_now = available_now;
    }
  }
  return best;
}

Response handle_request(dr_evt::Simulation &simulation,
                        const Request &request) {
  Response response;
  try {
    if (request.operation == Operation::Snapshot) {
      simulation.advance_to(request.target_time);
      const auto window = simulation.get_backfill_window();
      response.window.current_time = window.current_time;
      response.window.available_nodes = window.available_nodes;
      response.window.shadow_time = window.shadow_time;
      for (const auto &release : window.releases)
        response.window.releases.push_back(
            {release.time, release.nodes_released});
      response.prediction_horizon =
          simulation.get_prediction_horizon(request.prediction_utilization);
    } else if (request.operation == Operation::Append) {
      response.job_idx = simulation.append_job(
          request.job.submit_time, request.job.num_nodes, request.job.queue,
          request.job.limit_time, request.job.duration);
      simulation.advance_to(request.job.submit_time);
    } else {
      simulation.advance_to(std::numeric_limits<dr_evt::sim_time_t>::max());
      const auto stats = simulation.get_statistics();
      response.statistics = {static_cast<std::uint64_t>(stats.jobs_submitted),
                             static_cast<std::uint64_t>(stats.jobs_completed),
                             stats.avg_turnaround_time,
                             stats.avg_bounded_slowdown,
                             stats.makespan};
    }
  } catch (const std::exception &error) {
    response.ok = false;
    response.error = error.what();
  }
  return response;
}

void worker(const Options &options, const std::vector<System> &systems,
            int rank) {
  const std::size_t index = static_cast<std::size_t>(rank - 1);
  dr_evt::Sim_Params params;
  params.m_infile = options.jobs;
  params.m_total_nodes = systems[index].physical_nodes;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = dr_evt::RunTimeMode::ACTUAL;
  params.m_backfill_policy = dr_evt::BackfillPolicy::EASY;
  params.m_priority_policy = dr_evt::PriorityPolicy::FCFS;
  params.m_queue_impl = dr_evt::QueueImplementation::CIRCULAR;
  params.m_verbose = false;
  dr_evt::Simulation simulation(params);

  while (true) {
    const Request request = receive_serialized<Request>(0);
    const bool finish = request.operation == Operation::Finish;
    send_serialized(handle_request(simulation, request), 0);
    if (finish)
      break;
  }
}

std::vector<Response> call_all(const Request &request, int worker_count) {
  for (int rank = 1; rank <= worker_count; ++rank)
    send_serialized(request, rank);
  std::vector<Response> responses;
  responses.reserve(static_cast<std::size_t>(worker_count));
  for (int rank = 1; rank <= worker_count; ++rank) {
    auto response = receive_serialized<Response>(rank);
    if (!response.ok)
      throw std::runtime_error("worker " + std::to_string(rank) + ": " +
                               response.error);
    responses.push_back(std::move(response));
  }
  return responses;
}

std::string csv_field(const std::string &value) {
  if (value.find_first_of(",\"\r\n") == std::string::npos)
    return value;
  std::string escaped = "\"";
  for (const char character : value) {
    escaped.push_back(character);
    if (character == '"')
      escaped.push_back('"');
  }
  escaped.push_back('"');
  return escaped;
}

void controller(const Options &options, const std::vector<System> &systems,
                int worker_count) {
  const auto jobs = read_jobs(options.jobs);
  const auto requirements = read_application_requirements(options.applications);
  const auto prediction =
      options.dispatch_policy == DispatchPolicy::WaitTimeOnly
          ? options.ground_truth
          : options.prediction;
  const auto catalog = read_workloads(options.ground_truth, prediction, systems,
                                      requirements);
  const auto largest_system = std::max_element(
      systems.begin(), systems.end(), [](const auto &left, const auto &right) {
        return left.physical_nodes < right.physical_nodes;
      });
  std::mt19937_64 generator(options.seed);
  std::ofstream output_file;
  std::ostream *output = &std::cout;
  if (options.output) {
    output_file.open(*options.output);
    if (!output_file)
      throw std::runtime_error("cannot open output: " + *options.output);
    output = &output_file;
  }
  *output << "job_id,submit_time,num_nodes,effective_nodes,duration,time_limit,"
             "App,Args,Ranks,sys_requirement,system_id,"
             "execution_mode,"
             "ground_truth_relative_performance,"
             "predicted_relative_performance,"
             "prediction_fallback,"
             "estimated_wait,estimated_duration,actual_duration,"
             "predicted_time_limit,actual_time_limit,submitted_time_limit,"
             "time_limit_doublings,"
             "predicted_turnaround,job_idx\n";
  *output << std::setprecision(17);

  double total_run_time = 0.0;
  double total_speedup = 0.0;
  std::uint64_t dispatched_jobs = 0;
  std::uint64_t dropped_jobs = 0;

  for (const auto &job : jobs) {
    Job dispatch_job = job;
    dispatch_job.num_nodes =
        std::min(job.num_nodes, largest_system->physical_nodes);
    if (dispatch_job.num_nodes != job.num_nodes)
      std::cerr << "warning: job_id=" << job.id
                << " requested_nodes=" << job.num_nodes
                << " effective_nodes=" << dispatch_job.num_nodes
                << " reason=exceeds_largest_system\n";
    Request snapshot;
    snapshot.operation = Operation::Snapshot;
    snapshot.target_time = job.submit_time;
    snapshot.prediction_utilization = options.prediction_utilization;
    const auto snapshots = call_all(snapshot, worker_count);
    const auto &workload = sample_workload(catalog, generator);
    const auto selected =
        choose_system(dispatch_job, workload, systems, snapshots,
                      options.max_time_limit, options.dispatch_policy,
                      options.wall_time_policy);
    if (!selected) {
      ++dropped_jobs;
      std::cerr << "dropped: job_id=" << job.id << " app=" << workload.app
                << " reason=no_system_within_max_time_limit"
                << " max_time_limit=" << options.max_time_limit << '\n';
      continue;
    }
    const Choice &choice = *selected;
    ++dispatched_jobs;
    total_run_time += choice.actual_duration;
    total_speedup += choice.ground_truth_relative_performance;

    Request append;
    append.operation = Operation::Append;
    append.job = {job.submit_time, dispatch_job.num_nodes, job.queue,
                  choice.actual_duration, choice.submitted_time_limit};
    send_serialized(append, static_cast<int>(choice.index) + 1);
    const Response response =
        receive_serialized<Response>(static_cast<int>(choice.index) + 1);
    if (!response.ok)
      throw std::runtime_error("worker " + std::to_string(choice.index + 1) +
                               ": " + response.error);
    *output << csv_field(job.id) << ',' << job.submit_time << ','
            << job.num_nodes << ',' << dispatch_job.num_nodes << ','
            << job.duration << ',' << job.limit_time << ','
            << csv_field(workload.app) << ',' << csv_field(workload.args) << ','
            << workload.ranks << ',' << to_string(workload.requirement) << ','
            << csv_field(systems[choice.index].id) << ','
            << choice.execution_mode << ','
            << choice.ground_truth_relative_performance << ','
            << choice.predicted_relative_performance << ','
            << (choice.prediction_fallback ? "true" : "false") << ','
            << choice.estimated_wait << ',' << choice.estimated_duration << ','
            << choice.actual_duration << ',' << choice.predicted_time_limit
            << ',' << choice.actual_time_limit << ','
            << choice.submitted_time_limit << ','
            << choice.time_limit_doublings << ','
            << choice.predicted_turnaround << ',' << response.job_idx << '\n';
  }

  Request finish;
  finish.operation = Operation::Finish;
  const auto responses = call_all(finish, worker_count);
  std::uint64_t completed_jobs = 0;
  double total_turnaround = 0.0;
  double total_bounded_slowdown = 0.0;
  for (std::size_t i = 0; i < responses.size(); ++i) {
    const auto &stats = responses[i].statistics;
    completed_jobs += stats.jobs_completed;
    total_turnaround += stats.avg_turnaround_time * stats.jobs_completed;
    total_bounded_slowdown +=
        stats.avg_bounded_slowdown * stats.jobs_completed;
    std::cerr << systems[i].id << ": submitted=" << stats.jobs_submitted
              << " completed=" << stats.jobs_completed
              << " makespan=" << stats.makespan << '\n';
  }
  if (completed_jobs != dispatched_jobs)
    throw std::runtime_error("completed job count does not match dispatched "
                             "job count");
  const double denominator = static_cast<double>(dispatched_jobs);
  std::cerr << std::setprecision(8) << "overall: jobs=" << dispatched_jobs
            << " dropped_jobs=" << dropped_jobs
            << " average_turnaround_time="
            << (dispatched_jobs == 0 ? 0.0 : total_turnaround / denominator)
            << " average_bounded_slowdown="
            << (dispatched_jobs == 0 ? 0.0
                                     : total_bounded_slowdown / denominator)
            << " average_run_time="
            << (dispatched_jobs == 0 ? 0.0 : total_run_time / denominator)
            << " average_speedup="
            << (dispatched_jobs == 0 ? 0.0 : total_speedup / denominator)
            << '\n';
}

} // namespace

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  int size = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  try {
    Options options = parse_options(argc, argv);
    if (size < 2)
      throw std::invalid_argument("at least two MPI ranks are required");
    const auto worker_count = static_cast<std::size_t>(size - 1);
    const auto systems = read_systems(options.systems_table);
    if (systems.size() != worker_count)
      throw std::invalid_argument(
          "--systems must contain exactly one row per worker rank");

    if (rank == 0)
      controller(options, systems, size - 1);
    else
      worker(options, systems, rank);
    MPI_Finalize();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rank " << rank << " error: " << error.what() << '\n';
    MPI_Abort(MPI_COMM_WORLD, 1);
    return 1;
  }
}
