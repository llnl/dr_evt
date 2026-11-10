#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>

using namespace dr_evt;
using Clock = std::chrono::steady_clock;

// Benchmark adapter: the public exact API replays everything stored in its
// input state. Give it only observable history and time-limit future durations.
// Preparation is included in each measured side-simulation query.
class ObservableSimulation : public Simulation {
public:
  using Simulation::Simulation;

  std::vector<Exact_Start_Result>
  predict_starts(const std::vector<job_no_t> &ids) {
    std::stringstream snapshot;
    save_checkpoint_impl(snapshot, false);
    ObservableSimulation model(m_params);
    model.load_checkpoint_impl(snapshot, false);
    model.m_params.set_outfile("");
    model.m_params.set_resource_trace("");
    model.m_params.m_run_time_mode = RunTimeMode::LIMIT;
    if (model.m_params.m_node_release_delay != 0)
      throw std::runtime_error("Observable fixture requires immediate node release");
    for (auto &job : model.m_trace.data()) {
      if (convert_epoch<sim_time_t>(job.get_submit_time()) > model.m_current_time)
        throw std::runtime_error("Prediction input contains a future submission");
      if (!job.is_scheduled()) job.set_actual_run_time(job.get_limit_time());
    }
    auto &events = const_cast<event_q_t &>(model.m_trace.pending_events());
    events.clear();
    for (const auto &[id, running] : model.m_running_jobs) {
      auto &job = model.m_trace.job_at(id);
      job.set_actual_run_time(job.get_limit_time());
      job.compute_end_time();
      const double end = running.start_time + job.get_limit_time();
      if (end <= model.m_current_time)
        throw std::runtime_error("Running fixture exceeded its time limit");
      events.emplace(id, end, departure);
      model.m_appended_job_query_records[id-model.m_first_appended_job_idx].end_time = end;
    }
    return model.get_exact_job_start_times(ids);
  }
};

static std::vector<Simulation::Job_Append_Request>
read_jobs(const std::string &path) {
  std::ifstream input(path);
  std::string line;
  std::getline(input, line);
  std::vector<Simulation::Job_Append_Request> jobs;
  while (std::getline(input, line)) {
    std::replace(line.begin(), line.end(), ',', ' ');
    std::istringstream fields(line);
    double arrival, limit, actual;
    unsigned nodes;
    if (!(fields >> arrival >> nodes >> limit >> actual))
      throw std::runtime_error("Invalid benchmark input row");
    jobs.push_back({arrival, nodes, "1", limit, actual});
  }
  return jobs;
}

static std::unique_ptr<ObservableSimulation>
make_sim(const Sim_Params &params,
         const std::vector<Simulation::Job_Append_Request> &jobs) {
  auto sim = std::make_unique<ObservableSimulation>(params);
  sim->get_trace().load_data(0);
  const auto ids = sim->append_jobs(jobs);
  if (ids.size() != jobs.size()) throw std::runtime_error("Append count mismatch");
  return sim;
}

template <typename Query>
static double measure(Query query, size_t repetitions, size_t &checksum) {
  checksum += query(); // warm reusable allocations before timing
  const auto begin = Clock::now();
  for (size_t i = 0; i < repetitions; ++i) checksum += query();
  return std::chrono::duration<double>(Clock::now() - begin).count() /
         repetitions;
}

int main(int argc, char **argv) {
  if (argc < 5) throw std::runtime_error("Usage: evaluate jobs.csv output-dir mode [simulator options]");
  auto jobs = read_jobs(argv[1]);
  const bool actual_execution = std::string(argv[3]) == "actual";
  if (!actual_execution)
    for (auto &job : jobs) job.actual_run_time = job.limit_time;
  const std::string directory = argv[2];
  std::vector<char *> config_args{argv[0], argv[1]};
  for (int i = 4; i < argc; ++i) config_args.push_back(argv[i]);
  Sim_Params params;
  int config_argc = static_cast<int>(config_args.size());
  char **config_argv = config_args.data();
  params.getopt(config_argc, config_argv);
  params.m_run_time_mode = actual_execution ? RunTimeMode::ACTUAL : RunTimeMode::LIMIT;
  params.m_infile = directory + "/empty.csv";
  params.set_outfile(directory + "/unused.csv");
  params.m_job_store_capacity = jobs.size() + 1;
  std::ofstream(params.m_infile) << "job_submit_time,num_nodes,time_limit\n";
  std::vector<job_no_t> all_ids(jobs.size());
  std::iota(all_ids.begin(), all_ids.end(), job_no_t{0});
  double terminal = 1;
  for (const auto &job : jobs)
    terminal += job.limit_time + job.submit_time / jobs.size();
  terminal += jobs.back().submit_time + 1000;

  auto truth = make_sim(params, jobs);
  truth->advance_to(terminal);
  const auto actual = truth->get_job_statuses(all_ids);
  double last_start = 0;
  for (const auto &job : actual) {
    if (!job.start_time || !job.end_time)
      throw std::runtime_error("Ground-truth simulation did not drain");
    last_start = std::max(last_start, *job.start_time);
  }
  std::ofstream ground(directory + "/ground_truth.csv");
  ground << std::setprecision(17) << "job_idx,submit,nodes,time_limit,actual_run_time,start,end\n";
  for (size_t i = 0; i < jobs.size(); ++i)
    ground << i << ',' << jobs[i].submit_time << ',' << jobs[i].num_nodes << ','
           << jobs[i].limit_time << ',' << *jobs[i].actual_run_time << ','
           << *actual[i].start_time << ',' << *actual[i].end_time << '\n';

  std::vector<double> points;
  for (size_t i = 0; i <= 10; ++i)
    points.push_back(jobs[std::min(jobs.size()-1, i*jobs.size()/10)].submit_time);
  for (double fraction : {0.25, 0.5, 0.75})
    points.push_back(jobs.back().submit_time + fraction *
                     std::max(0.0, last_start - jobs.back().submit_time));
  std::sort(points.begin(), points.end());
  points.erase(std::unique(points.begin(), points.end()), points.end());

  std::ofstream rows(directory + "/predictions.csv");
  rows << std::setprecision(17)
       << "snapshot,time,estimated_at,pending,job_idx,api,kind,predicted_start,true_start\n";
  std::ofstream timings(directory + "/timings.csv");
  timings << std::setprecision(17)
          << "snapshot,time,pending,known_queue,api,repetitions,seconds_per_query\n";
  std::ofstream shared_timings(directory + "/shared_timings.csv");
  shared_timings << std::setprecision(17)
                 << "snapshot,time,pending,shared_jobs,known_queue,api,repetitions,seconds_per_query\n";
  size_t checksum = 0, snapshot = 0;
  for (const auto time : points) {
    const auto visible_end = std::upper_bound(jobs.begin(), jobs.end(), time,
        [](double t, const auto &job) { return t < job.submit_time; });
    const std::vector<Simulation::Job_Append_Request> visible(jobs.begin(), visible_end);
    const std::vector<Simulation::Job_Append_Request> future(visible_end, jobs.end());
    auto sim = make_sim(params, visible);
    sim->advance_to(time);
    const auto cached = sim->get_estimated_job_starts();
    std::vector<job_no_t> ids;
    for (const auto &job : cached.jobs) ids.push_back(job.job_idx);
    if (ids.empty()) continue;
    auto reference = make_sim(params, visible);
    reference->advance_to(time);
    reference->append_jobs(future);
    reference->advance_to(terminal);
    const auto continuation = reference->get_job_statuses(all_ids);
    const auto fresh = sim->get_job_statuses(ids, 0, false);
    const auto bounded = sim->get_job_statuses(ids, ids.size(), false);
    const auto extended = sim->get_job_statuses(ids);
    const auto exact = sim->predict_starts(ids);
    const auto before = sim->get_statistics();
    const auto write = [&](job_no_t id, const char *api,
                           Simulation::Start_Time_Kind kind,
                           std::optional<double> prediction) {
      rows << snapshot << ',' << time << ',';
      if (cached.estimated_at) rows << *cached.estimated_at;
      rows << ',' << ids.size() << ',' << id << ',' << api << ','
           << static_cast<int>(kind) << ',';
      if (prediction) rows << *prediction;
      rows << ',' << *continuation[id].start_time << '\n';
    };
    for (const auto &job : cached.jobs)
      write(job.job_idx, "cached", job.start_time_kind, job.expected_start_time);
    for (const auto &job : fresh)
      write(job.job_idx, "fresh", job.start_time_kind, job.expected_start_time);
    for (const auto &job : bounded)
      write(job.job_idx, "fresh_depth", job.start_time_kind, job.expected_start_time);
    for (const auto &job : extended)
      write(job.job_idx, "extended", job.start_time_kind, job.expected_start_time);
    for (const auto &job : exact)
      write(job.job_idx, "exact", Simulation::Start_Time_Kind::NONE, job.start_time);
    const auto timing = [&](const char *name, size_t repetitions, auto query) {
      const auto seconds = measure(query, repetitions, checksum);
      timings << snapshot << ',' << time << ',' << ids.size() << ','
              << sim->get_active_job_count() << ',' << name << ','
              << repetitions << ',' << seconds << '\n';
    };
    timing("cached", 500, [&] { return sim->get_estimated_job_starts().jobs.size(); });
    timing("fresh", 50, [&] { return sim->get_job_statuses(ids, 0, false).size(); });
    timing("fresh_depth", 50, [&] { return sim->get_job_statuses(ids, ids.size(), false).size(); });
    timing("extended", 50, [&] { return sim->get_job_statuses(ids).size(); });
    timing("exact", 3, [&] { return sim->predict_starts(ids).size(); });
    std::vector<job_no_t> shared_ids;
    for (const auto &job : cached.jobs)
      if (job.expected_start_time && std::isfinite(*job.expected_start_time))
        shared_ids.push_back(job.job_idx);
    if (!shared_ids.empty()) {
      const auto shared_fresh = sim->get_job_statuses(shared_ids, 0, false);
      const auto shared_bounded = sim->get_job_statuses(shared_ids, ids.size(), false);
      const auto shared_exact = sim->predict_starts(shared_ids);
      for (size_t i = 0; i < shared_ids.size(); ++i) {
        const auto id = shared_ids[i];
        const auto original = std::find_if(fresh.begin(), fresh.end(),
            [id](const auto &job) { return job.job_idx == id; });
        const auto original_exact = std::find_if(exact.begin(), exact.end(),
            [id](const auto &job) { return job.job_idx == id; });
        if (original == fresh.end() || shared_fresh[i].job_idx != id ||
            shared_bounded[i].job_idx != id || shared_exact[i].job_idx != id ||
            shared_fresh[i].expected_start_time != original->expected_start_time ||
            shared_bounded[i].expected_start_time != original->expected_start_time ||
            original_exact == exact.end() ||
            shared_exact[i].start_time != original_exact->start_time)
          throw std::runtime_error("Shared-ID query changed predictions");
      }
      const auto shared_timing = [&](const char *name, size_t repetitions, auto query) {
        const auto seconds = measure(query, repetitions, checksum);
        shared_timings << snapshot << ',' << time << ',' << ids.size() << ','
                       << shared_ids.size() << ',' << sim->get_active_job_count()
                       << ',' << name << ',' << repetitions << ',' << seconds << '\n';
      };
      shared_timing("cached_filtered", 500, [&] {
        auto response = sim->get_estimated_job_starts();
        response.jobs.erase(std::remove_if(response.jobs.begin(), response.jobs.end(),
            [](const auto &job) {
              return !job.expected_start_time || !std::isfinite(*job.expected_start_time);
            }), response.jobs.end());
        return response.jobs.size();
      });
      shared_timing("fresh_shared", 50, [&] {
        return sim->get_job_statuses(shared_ids, 0, false).size();
      });
      // Bound to the eligible prefix, not the number of requested shared IDs:
      // cached predictions need not occupy a contiguous queue prefix.
      shared_timing("fresh_shared_depth", 50, [&] {
        return sim->get_job_statuses(shared_ids, ids.size(), false).size();
      });
      shared_timing("extended_shared", 50, [&] {
        return sim->get_job_statuses(shared_ids).size();
      });
      shared_timing("exact_shared", 3, [&] {
        return sim->predict_starts(shared_ids).size();
      });
    }
    const auto after = sim->get_statistics();
    if (after.current_time != before.current_time ||
        after.jobs_started != before.jobs_started ||
        after.jobs_completed != before.jobs_completed)
      throw std::runtime_error("Query altered live simulation");
    const auto unchanged = sim->get_estimated_job_starts();
    if (unchanged.estimated_at != cached.estimated_at ||
        unchanged.jobs.size() != cached.jobs.size())
      throw std::runtime_error("Query altered cached plan");
    for (size_t i = 0; i < cached.jobs.size(); ++i)
      if (unchanged.jobs[i].job_idx != cached.jobs[i].job_idx ||
          unchanged.jobs[i].expected_start_time != cached.jobs[i].expected_start_time ||
          unchanged.jobs[i].start_time_kind != cached.jobs[i].start_time_kind)
        throw std::runtime_error("Query altered cached prediction");
    sim->append_jobs(future);
    sim->advance_to(terminal);
    const auto observed = sim->get_job_statuses(all_ids);
    for (size_t i = 0; i < continuation.size(); ++i)
      if (continuation[i].start_time != observed[i].start_time ||
          continuation[i].end_time != observed[i].end_time)
        throw std::runtime_error("Query changed actual continuation results");
    ++snapshot;
  }
  std::cout << "PASS snapshots=" << snapshot << " jobs=" << jobs.size()
            << " checksum=" << checksum << '\n';
}
