#include "params/sim_params.hpp"
#include "sim/capacity_schedule.hpp"
#include "sim/scheduler_easy_power.hpp"
#include "sim/sim.hpp"
#include "trace/trace.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace dr_evt;

namespace {

EASYPowerReplayResult replay_jobs(const std::vector<EASYPowerJob> &waiting,
                                  const std::vector<EASYPowerJob> &running,
                                  double utilization, num_nodes_t total_nodes,
                                  const std::vector<Capacity_Change> &changes,
                                  sim_time_t current_time) {
  if (waiting.empty() && running.empty()) {
    return {0.0, 0.0};
  }

  const double effective_utilization = utilization >= 0.1 ? utilization : 1.0;
  double queued_node_seconds = 0.0;
  for (const auto &job : waiting) {
    queued_node_seconds += job.nodes * job.remaining_time;
  }

  num_nodes_t scheduled_capacity = total_nodes;
  struct ReplayEvent {
    num_nodes_t released_nodes = 0;
    std::optional<num_nodes_t> new_capacity;
  };
  std::map<tdiff_t, ReplayEvent> events;
  num_nodes_t running_nodes = 0;
  tdiff_t last_release = 0.0;
  for (const auto &job : running) {
    running_nodes += job.nodes;
    events[job.remaining_time].released_nodes += job.nodes;
    last_release = std::max(last_release, job.remaining_time);
  }
  for (const auto &change : changes) {
    if (change.time <= current_time) {
      scheduled_capacity = change.total_nodes;
    } else {
      events[change.time - current_time].new_capacity = change.total_nodes;
    }
  }
  num_nodes_t maximum_future_capacity = scheduled_capacity;
  for (const auto &[event_time, event] : events) {
    (void)event_time;
    if (event.new_capacity) {
      maximum_future_capacity =
          std::max(maximum_future_capacity, *event.new_capacity);
    }
  }
  for (const auto &job : waiting) {
    if (job.nodes > maximum_future_capacity) {
      throw std::runtime_error(
          "a waiting job cannot fit any remaining scheduled capacity");
    }
  }

  double supplied_node_seconds = 0.0;
  tdiff_t previous = 0.0;
  tdiff_t horizon = 0.0;
  bool queue_drained = queued_node_seconds == 0.0;
  for (const auto &[event_time, event] : events) {
    const num_nodes_t available = scheduled_capacity > running_nodes
                                      ? scheduled_capacity - running_nodes
                                      : 0;
    const double rate = effective_utilization * available;
    const double interval = event_time - previous;
    if (!queue_drained && rate > 0.0 &&
        supplied_node_seconds + rate * interval >= queued_node_seconds) {
      horizon = previous + (queued_node_seconds - supplied_node_seconds) / rate;
      queue_drained = true;
      break;
    }
    supplied_node_seconds += rate * interval;
    running_nodes -= event.released_nodes;
    if (event.new_capacity) {
      scheduled_capacity = *event.new_capacity;
    }
    previous = event_time;
  }
  if (!queue_drained) {
    if (scheduled_capacity == 0) {
      throw std::runtime_error(
          "cannot estimate a positive queue horizon when the capacity "
          "schedule ends at zero nodes");
    }
    horizon = previous + (queued_node_seconds - supplied_node_seconds) /
                             (effective_utilization * scheduled_capacity);
  } else if (queued_node_seconds == 0.0) {
    horizon = last_release;
  }

  double running_energy = 0.0;
  for (const auto &job : running) {
    running_energy +=
        job.predicted_power * std::min(job.remaining_time, horizon);
  }
  return {horizon, running_energy};
}

struct TelemetryState {
  std::vector<double> actual_runtime;
  std::vector<double> time_limit;
  EASYPowerReplayResult estimated{0.0, 0.0};
  EASYPowerReplayResult actual{0.0, 0.0};
  double estimated_waiting_energy = 0.0;
  double actual_waiting_energy = 0.0;
  double utilization = 0.0;
  size_t waiting_jobs = 0;
  size_t running_jobs = 0;
  sim_time_t current_time = 0.0;
  std::ofstream output;
};

struct JobMetadata {
  std::vector<double> powers;
  std::vector<double> actual_runtime;
  std::vector<double> time_limit;
};

JobMetadata load_job_metadata(const std::vector<std::string> &trace_files) {
  JobMetadata metadata;
  for (const std::string &trace_file : trace_files) {
    // Keep only the three scheduler/telemetry vectors after each batch. This
    // mirrors progressive simulation's global job-ID order without retaining
    // all parsed trace records in memory at the same time.
    PconTrace source(trace_file, "simple", "epoch", "+00:00");
    if (source.load_data() != EXIT_SUCCESS) {
      throw std::runtime_error("failed to load Pcon metadata from " +
                               trace_file);
    }
    for (const auto &job : source.data()) {
      metadata.powers.push_back(job.pcon().avgpcon);
      metadata.actual_runtime.push_back(job.get_actual_run_time());
      metadata.time_limit.push_back(job.get_limit_time());
    }
  }
  return metadata;
}

class InstrumentedEASYPowerScheduler final : public EASYPowerScheduler {
public:
  InstrumentedEASYPowerScheduler(
      num_nodes_t total_nodes, size_t initial_job_count,
      size_t num_max_candidates, double maximum_power, double initial_target,
      job_power_function_t power_function,
      const std::shared_ptr<TelemetryState> &telemetry,
      const std::vector<Capacity_Change> &capacity_changes,
      tdiff_t candidate_time_window, size_t initial_capacity,
      CircularOverflowPolicy overflow_policy, bool cap_backfill_power,
      bool cap_fcfs_power)
      : EASYPowerScheduler(
            total_nodes, initial_job_count, num_max_candidates, maximum_power,
            initial_target, 1.0, 1.0, maximum_power * maximum_power,
            std::move(power_function),
            [telemetry,
             &capacity_changes](const std::vector<EASYPowerJob> &waiting,
                                const std::vector<EASYPowerJob> &running,
                                double, num_nodes_t nodes) {
              telemetry->waiting_jobs = waiting.size();
              telemetry->running_jobs = running.size();
              telemetry->estimated_waiting_energy = 0.0;
              telemetry->actual_waiting_energy = 0.0;

              std::vector<EASYPowerJob> actual_waiting;
              std::vector<EASYPowerJob> actual_running;
              actual_waiting.reserve(waiting.size());
              actual_running.reserve(running.size());
              for (const auto &job : waiting) {
                telemetry->estimated_waiting_energy +=
                    job.predicted_power * job.remaining_time;
                const double duration =
                    telemetry->actual_runtime.at(job.job_id);
                telemetry->actual_waiting_energy +=
                    job.predicted_power * duration;
                actual_waiting.push_back(
                    {job.job_id, duration, job.nodes, job.predicted_power});
              }
              for (const auto &job : running) {
                const double elapsed =
                    std::max(0.0, telemetry->time_limit.at(job.job_id) -
                                      job.remaining_time);
                const double remaining = std::max(
                    0.0, telemetry->actual_runtime.at(job.job_id) - elapsed);
                actual_running.push_back(
                    {job.job_id, remaining, job.nodes, job.predicted_power});
              }
              telemetry->estimated =
                  replay_jobs(waiting, running, telemetry->utilization, nodes,
                              capacity_changes, telemetry->current_time);
              telemetry->actual = replay_jobs(
                  actual_waiting, actual_running, telemetry->utilization, nodes,
                  capacity_changes, telemetry->current_time);
              return telemetry->estimated;
            },
            initial_capacity, overflow_policy, cap_backfill_power,
            cap_fcfs_power),
        telemetry_(telemetry), capacity_changes_(capacity_changes),
        candidate_time_window_(candidate_time_window), allocated_area_(0.0),
        capacity_area_(0.0), accounting_time_(0.0), accounted_nodes_(0),
        accounted_capacity_(capacity_at(0.0)) {
    if (!std::isfinite(candidate_time_window_) ||
        candidate_time_window_ < 0.0) {
      throw std::invalid_argument(
          "candidate time window must be finite and nonnegative");
    }
  }

  double capacity_aware_resource_area() const { return allocated_area_; }
  double capacity_aware_utilization() const {
    return capacity_area_ > 0.0 ? allocated_area_ / capacity_area_ : 0.0;
  }

protected:
  void on_jobs_became_eligible(size_t newly_eligible_begin, size_t eligible_end,
                               num_nodes_t available_nodes,
                               const running_jobs_t &running_jobs,
                               sim_time_t current_time) override {
    update_accounting(running_jobs, current_time);
    EASYPowerScheduler::on_jobs_became_eligible(newly_eligible_begin,
                                                eligible_end, available_nodes,
                                                running_jobs, current_time);
  }

  void on_backfill_candidates_ready(
      const backfill_candidates_t &candidates, num_nodes_t available_nodes,
      const running_jobs_t &effective_running_jobs, sim_time_t current_time,
      bool fcfs_jobs_started) override {
    update_accounting(effective_running_jobs, current_time);
    EASYPowerScheduler::on_backfill_candidates_ready(
        candidates, available_nodes, effective_running_jobs, current_time,
        fcfs_jobs_started);
  }

  std::optional<job_no_t>
  select_backfill_candidate(const backfill_candidates_t &candidates,
                            num_nodes_t available_nodes,
                            const running_jobs_t &effective_running_jobs,
                            sim_time_t current_time) override {
    if (candidate_time_window_ == 0.0 || candidates.empty()) {
      return EASYPowerScheduler::select_backfill_candidate(
          candidates, available_nodes, effective_running_jobs, current_time);
    }

    // The temporal window starts at the blocked FCFS head's submission time.
    // Because the queue is in FCFS order, this is an arrival-time span through
    // the queue, not a job runtime limit or a power-analysis window.
    const auto &queue = queued_jobs();
    const size_t eligible_end = eligible_job_end();
    size_t head_index = 0;
    while (head_index < eligible_end && queue[head_index].removed) {
      ++head_index;
    }
    if (head_index == eligible_end) {
      return std::nullopt;
    }
    const sim_time_t cutoff =
        queue[head_index].submit_time + candidate_time_window_;
    backfill_candidates_t within_window;
    within_window.reserve(candidates.size());
    size_t queue_index = head_index;
    for (const auto &candidate : candidates) {
      while (queue_index < eligible_end &&
             queue[queue_index].job_id != candidate.first) {
        ++queue_index;
      }
      if (queue_index == eligible_end) {
        throw std::logic_error("EASYPower candidate is absent from wait queue");
      }
      if (queue[queue_index].submit_time <= cutoff) {
        within_window.push_back(candidate);
      }
    }
    return EASYPowerScheduler::select_backfill_candidate(
        within_window, available_nodes, effective_running_jobs, current_time);
  }

  void on_scheduling_cycle_complete(num_nodes_t available_nodes,
                                    const running_jobs_t &running_jobs,
                                    sim_time_t current_time) override {
    update_accounting(running_jobs, current_time);
    EASYPowerScheduler::on_scheduling_cycle_complete(
        available_nodes, running_jobs, current_time);

    double actual_target = 0.0;
    if (telemetry_->actual.horizon > 0.0) {
      actual_target =
          std::min(maximum_power(), (telemetry_->actual.running_energy +
                                     telemetry_->actual_waiting_energy) /
                                        telemetry_->actual.horizon);
    }
    telemetry_->output << std::setprecision(17) << current_time << ','
                       << telemetry_->utilization << ','
                       << telemetry_->waiting_jobs << ','
                       << telemetry_->running_jobs << ','
                       << telemetry_->estimated.horizon << ','
                       << telemetry_->actual.horizon << ',' << power_target()
                       << ',' << actual_target << '\n';
  }

private:
  void update_accounting(const running_jobs_t &running_jobs,
                         sim_time_t current_time) {
    const tdiff_t duration = current_time - accounting_time_;
    if (duration < 0.0) {
      throw std::logic_error("EASYPower capacity accounting moved backward");
    }
    allocated_area_ += static_cast<double>(accounted_nodes_) * duration;
    capacity_area_ +=
        static_cast<double>(std::max(accounted_nodes_, accounted_capacity_)) *
        duration;
    accounting_time_ = current_time;
    accounted_nodes_ = 0;
    for (const auto &[job_id, running] : running_jobs) {
      (void)job_id;
      accounted_nodes_ += running.nodes;
    }
    accounted_capacity_ = capacity_at(current_time);
    telemetry_->current_time = current_time;
    telemetry_->utilization =
        capacity_area_ > 0.0 ? allocated_area_ / capacity_area_ : 0.0;
  }
  num_nodes_t capacity_at(sim_time_t time) const {
    num_nodes_t capacity = m_total_nodes;
    for (const auto &change : capacity_changes_) {
      if (change.time > time) {
        break;
      }
      capacity = change.total_nodes;
    }
    return capacity;
  }

  std::shared_ptr<TelemetryState> telemetry_;
  const std::vector<Capacity_Change> &capacity_changes_;
  tdiff_t candidate_time_window_;
  double allocated_area_;
  double capacity_area_;
  sim_time_t accounting_time_;
  num_nodes_t accounted_nodes_;
  num_nodes_t accounted_capacity_;
};

void print_stats(const char *name, const PconSimulation::Statistics &stats,
                 std::optional<double> resource_area = std::nullopt,
                 std::optional<double> utilization = std::nullopt) {
  std::cout << std::setprecision(15) << name
            << ",jobs_completed=" << stats.jobs_completed
            << ",avg_wait_seconds=" << stats.avg_wait_time
            << ",avg_turnaround_seconds=" << stats.avg_turnaround_time
            << ",makespan=" << stats.makespan << ",resource_area_node_seconds="
            << resource_area.value_or(stats.resource_area)
            << ",utilization=" << utilization.value_or(stats.utilization)
            << '\n';
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 8) {
    std::cerr
        << "usage: easypower_experiment "
           "{easy|easypower|easy-progressive|easypower-progressive} "
           "INPUT_OR_LIST OUTPUT_DIR "
           "TOTAL_NODES CANDIDATE_LIMIT MAX_POWER_W INITIAL_TARGET_W "
           "[CANDIDATE_TIME_WINDOW_S] [CAPACITY_SCHEDULE.csv] "
           "[MAX_TIME_EPOCH_S] [--cap_backfill_power] [--cap_fcfs_power]\n"
           "  Progressive modes interpret INPUT_OR_LIST as an ordered file "
           "list and run the listed batches as one workload.\n"
           "  A zero time window means unlimited. A positive window limits "
           "candidates to submission times no later than the blocked FCFS "
           "head's submission time plus the window.\n";
    return 2;
  }

  try {
    const std::string mode = argv[1];
    const std::string input = argv[2];
    const std::filesystem::path output = argv[3];
    const auto total_nodes = static_cast<num_nodes_t>(std::stoul(argv[4]));
    const auto candidate_limit = static_cast<size_t>(std::stoull(argv[5]));
    const double maximum_power = std::stod(argv[6]);
    const double initial_target = std::stod(argv[7]);
    bool cap_backfill_power = false;
    bool cap_fcfs_power = false;
    std::vector<std::string> optional_positionals;
    for (int index = 8; index < argc; ++index) {
      const std::string argument = argv[index];
      if (argument == "--cap-backfill-power") {
        cap_backfill_power = true;
      } else if (argument == "--cap_backfill_power") {
        cap_backfill_power = true;
      } else if (argument == "--cap-fcfs-power") {
        cap_fcfs_power = true;
      } else if (argument == "--cap_fcfs_power") {
        cap_fcfs_power = true;
      } else if (!argument.empty() && argument.front() == '-') {
        throw std::invalid_argument("unknown option: " + argument);
      } else {
        optional_positionals.push_back(argument);
      }
    }
    if (optional_positionals.size() > 3) {
      throw std::invalid_argument("too many positional arguments");
    }
    const double candidate_time_window =
        optional_positionals.empty() ? 0.0
                                     : std::stod(optional_positionals[0]);
    const std::string capacity_schedule =
        optional_positionals.size() >= 2 ? optional_positionals[1] : "";
    const std::optional<sim_time_t> max_time =
        optional_positionals.size() == 3
            ? std::optional<sim_time_t>(std::stod(optional_positionals[2]))
            : std::nullopt;
    std::filesystem::create_directories(output);

    const bool progressive =
        mode == "easy-progressive" || mode == "easypower-progressive";
    const bool run_easy = mode == "easy" || mode == "easy-progressive";
    const bool run_easypower =
        mode == "easypower" || mode == "easypower-progressive";
    if (!run_easy && !run_easypower) {
      std::cerr << "unknown mode: " << mode << '\n';
      return 2;
    }

    Sim_Params params;
    if (progressive) {
      params.set_infile_list(input);
    } else {
      params.m_infile = input;
    }
    const std::vector<std::string> single_trace = {input};
    const auto &trace_files =
        progressive ? params.m_infile_list_parsed : single_trace;
    JobMetadata metadata = load_job_metadata(trace_files);
    const std::vector<double> &powers = metadata.powers;

    auto telemetry = std::make_shared<TelemetryState>();
    telemetry->actual_runtime = std::move(metadata.actual_runtime);
    telemetry->time_limit = std::move(metadata.time_limit);

    params.m_total_nodes = total_nodes;
    params.m_trace_type = TraceType::PCON;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_run_time_mode = RunTimeMode::ACTUAL;
    params.m_backfill_policy = BackfillPolicy::EASY;
    params.m_num_max_candidates = candidate_limit;
    params.m_cap_backfill_power = cap_backfill_power;
    params.m_cap_fcfs_power = cap_fcfs_power;
    params.m_capacity_schedule = capacity_schedule;
    if (max_time) {
      if (!std::isfinite(*max_time) || *max_time < 0.0) {
        throw std::invalid_argument(
            "MAX_TIME_EPOCH_S must be finite and nonnegative");
      }
      params.m_max_time = *max_time;
      params.m_is_time_set = true;
    }
    const std::vector<Capacity_Change> capacity_changes =
        load_capacity_schedule(capacity_schedule, total_nodes);

    if (run_easy) {
      params.set_outfile((output / "jobs.csv").string());
      params.set_resource_trace((output / "resources.csv").string());
      auto *simulation = new PconSimulation(params);
      simulation->run();
      const auto stats = simulation->get_statistics();
      simulation->write_simulated_trace();
      simulation->write_resource_trace(params.get_resource_trace());
      print_stats("easy", stats);
    } else {
      telemetry->output.open(output / "target_horizon.csv");
      if (!telemetry->output) {
        throw std::runtime_error("cannot open target_horizon.csv");
      }
      telemetry->output
          << "time,utilization,waiting_jobs,running_jobs,"
             "estimated_horizon_s,actual_duration_horizon_s,target_w,"
             "actual_duration_target_w\n";

      params.set_outfile((output / "jobs.csv").string());
      params.set_resource_trace((output / "resources.csv").string());
      auto scheduler = std::make_unique<InstrumentedEASYPowerScheduler>(
          params.m_total_nodes, progressive ? 0 : powers.size(),
          params.m_num_max_candidates, maximum_power, initial_target,
          [&powers](job_no_t id, sim_time_t, tdiff_t, num_nodes_t) {
            return powers.at(id);
          },
          telemetry, capacity_changes, candidate_time_window,
          params.m_wait_queue_capacity, params.m_wait_queue_overflow,
          params.m_cap_backfill_power, params.m_cap_fcfs_power);
      auto *easypower = scheduler.get();
      auto *simulation = new PconSimulation(params, std::move(scheduler));
      simulation->run();
      const auto stats = simulation->get_statistics();
      simulation->write_simulated_trace();
      simulation->write_resource_trace(params.get_resource_trace());
      print_stats("easypower", stats, easypower->capacity_aware_resource_area(),
                  easypower->capacity_aware_utilization());
      std::cout << "easypower,final_target_watts=" << easypower->power_target()
                << '\n';
      telemetry->output.flush();
      telemetry->output.close();
    }
    std::cout.flush();
    std::_Exit(0);
  } catch (const std::exception &error) {
    std::cerr << "easypower_experiment: " << error.what() << '\n';
    return 1;
  }
}
