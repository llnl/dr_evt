/******************************************************************************
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
/** @file placement_driver.cpp
 * @brief Native fixture protocol driving the actual Simulation scheduler and
 * event loop.
 * @details Whole nodes, equal priority, unlimited queue examination, no release
 * delay or dispatch latency. All scheduling and accounting use the main
 * simulator.
 */
#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ranges>
#include <string>
#include <unistd.h>

using namespace dr_evt;

/** @brief One stable job owner through submission, execution, and release. */
struct Job {
  sim_time_t submit;     ///< Submission time.
  num_nodes_t quantity;  ///< Requested whole-node quantity.
  tdiff_t limit;         ///< Estimated runtime used in planning.
  tdiff_t runtime;       ///< Actual runtime used for completion.
  bool launched = false; ///< Whether the job has ever started.
  sim_time_t finish = 0; ///< Actual completion time, meaningful after launch.
};

/** @brief Write IDs directly from a borrowed bitmap without an expanded side
 * list.
 * @param[in] mask Exact resource membership.
 * @param[in,out] out JSON output stream. */
static void write_ids(const BitmapAllocation &mask, std::ostream &out) {
  out << '[';
  bool first = true;
  for (size_t word = 0; word < mask.words().size(); ++word) {
    auto bits = mask.words()[word];
    while (bits) {
      if (!first)
        out << ',';
      out << word * 64 + std::countr_zero(bits);
      first = false;
      bits &= bits - 1;
    }
  }
  out << ']';
}

/** @brief Run a grouped-event FCFS/backfill traversal and emit exact decisions.
 * @tparam Order Compile-time node matching order.
 * @tparam CoarseFilter Compile-time lambda selecting coarse filtering.
 * @param[in,out] jobs Stable owning records, fully populated before borrowing.
 * @param[in] capacity Whole-node pool size.
 * @param[in] slots Maximum future reservations per scheduling pass.
 * @param[in] strict Whether a failed immediate head blocks later examination.
 * @param[in,out] out Schedule and quiescent snapshot JSON-lines stream.
 * @throws std::runtime_error if scheduling makes no progress.
 * @throws std::invalid_argument if resource accounting or planning is invalid.
 * @throws std::bad_alloc if planner or allocation storage cannot be allocated.
 */
template <NodeOrder Order, auto CoarseFilter = DisableCoarseFilter>
static void run(std::vector<Job> &jobs, num_nodes_t capacity, size_t slots,
                bool strict, std::ostream &out) {
  // Each process owns one header-only input; appended jobs use the normal API.
  char filename[] = "/tmp/dr-evt-placement-fixture-XXXXXX";
  const int descriptor = mkstemp(filename);
  if (descriptor < 0)
    throw std::runtime_error("cannot create fixture input");
  close(descriptor);
  struct Cleanup {
    const char *path;
    ~Cleanup() { std::remove(path); }
  } cleanup{filename};
  {
    std::ofstream input(filename);
    input << "job_submit_time,num_nodes,time_limit\n";
  }
  Sim_Params params;
  params.m_infile = filename;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_total_nodes = capacity;
  params.m_resource_model = "placement";
  params.m_placement_order = Order == NodeOrder::LowIdFirst ? "low" : "high";
  params.m_placement_coarse_filter = CoarseFilter();
  if (const auto *features = std::getenv("DR_EVT_PLACEMENT_OPTIMIZATIONS"))
    params.m_placement_optimizations = features;
  params.m_backfill_policy = strict       ? BackfillPolicy::NONE
                             : slots == 1 ? BackfillPolicy::EASY
                             : slots >= jobs.size()
                                 ? BackfillPolicy::CONSERVATIVE
                                 : BackfillPolicy::HYBRID;
  params.m_reservation_depth = slots;
  Simulation simulation(params);
  std::vector<job_no_t> ids;
  ids.reserve(jobs.size());
  for (const auto &job : jobs) {
#if DR_EVT_LEGACY_QUEUE_INPUT
    constexpr const char *queue = "pbatch";
#else
    constexpr const char *queue = "1";
#endif
    ids.push_back(simulation.append_job(job.submit, job.quantity, queue,
                                        job.limit, job.runtime));
  }
  sim_time_t time = jobs.empty() ? 0 : jobs.front().submit;
  size_t completed = 0;
  while (completed < jobs.size()) {
    simulation.advance_to(time);
    const auto statuses = simulation.get_job_statuses(ids);
    completed = 0;
    for (size_t index = 0; index < jobs.size(); ++index) {
      auto &job = jobs[index];
      const auto &status = statuses[index];
      if (status.state == Simulation::Job_State::COMPLETED)
        ++completed;
      if (!job.launched && status.start_time) {
        job.launched = true;
        job.finish = *status.end_time;
        out << "{\"schedule\":{\"job\":" << index
            << ",\"start\":" << *status.start_time
            << ",\"finish\":" << job.finish << ",\"node_ids\":";
        write_ids(simulation.placement_state()->jobs.at(ids[index]).allocation,
                  out);
        out << "}}\n";
      }
    }
    out << "{\"snapshot\":{\"time\":" << time << ",\"allocations\":[";
    bool first = true;
    for (const auto &[id, record] : simulation.placement_state()->jobs) {
      if (!first)
        out << ',';
      first = false;
      out << "{\"job\":" << id << ",\"node_ids\":";
      write_ids(record.allocation, out);
      out << '}';
    }
    out << "],\"reservations\":[";
    first = true;
    for (size_t index = 0; index < jobs.size(); ++index) {
      for (const auto &interval : simulation.placement_reservations()) {
        if (interval.job != ids[index] || interval.start <= time)
          continue;
        if (!first)
          out << ',';
        first = false;
        out << "{\"job\":" << index << ",\"start\":" << interval.start
            << ",\"finish\":" << interval.finish << ",\"node_ids\":";
        write_ids(interval.allocation(), out);
        out << '}';
      }
    }
    out << "]}}\n";
    if (completed == jobs.size())
      break;
    auto next = std::numeric_limits<sim_time_t>::infinity();
    for (size_t index = 0; index < jobs.size(); ++index) {
      if (!jobs[index].launched && jobs[index].submit > time)
        next = std::min(next, jobs[index].submit);
      if (statuses[index].end_time && *statuses[index].end_time > time)
        next = std::min(next, *statuses[index].end_time);
    }
    if (!std::isfinite(next))
      throw std::runtime_error("pending jobs cannot progress");
    time = next;
  }
}

/** @brief Read a narrow fixture protocol and run the validation driver.
 * @param[in] argc Number of command-line arguments.
 * @param[in] argv Optional --coarse-filter enables the coarse specialization
 * once, before processing the fixture; --exact-only explicitly keeps the
 * default.
 * @return Zero on success, one for invalid input or a scheduling error. */
int main(int argc, char **argv) {
  try {
    if (argc > 2 || (argc == 2 && std::string(argv[1]) != "--exact-only" &&
                     std::string(argv[1]) != "--coarse-filter"))
      throw std::invalid_argument(
          "only --coarse-filter or --exact-only is accepted");
    const bool use_coarse_filter =
        argc == 2 && std::string(argv[1]) == "--coarse-filter";
    num_nodes_t capacity;
    size_t slots, count;
    int strict;
    std::string order;
    if (!(std::cin >> capacity >> slots >> strict >> order >> count) ||
        capacity == 0 || (strict != 0 && strict != 1) ||
        (order != "low" && order != "high"))
      throw std::invalid_argument(
          "expected capacity slots strict low|high job-count");
    std::vector<Job> jobs;
    jobs.reserve(count);
    for (size_t index = 0; index < count; ++index) {
      Job job{};
      if (!(std::cin >> job.submit >> job.quantity >> job.limit >>
            job.runtime) ||
          !std::isfinite(job.submit) || !std::isfinite(job.limit) ||
          !std::isfinite(job.runtime) || job.submit < 0 || job.quantity == 0 ||
          job.quantity > capacity || job.runtime <= 0 ||
          job.limit < job.runtime ||
          (!jobs.empty() && job.submit < jobs.back().submit))
        throw std::invalid_argument(
            "invalid submission-ordered whole-node job");
      jobs.push_back(std::move(job));
    }
    std::cout << std::setprecision(17);
    if (use_coarse_filter) {
      if (order == "low")
        run<NodeOrder::LowIdFirst, EnableCoarseFilter>(jobs, capacity, slots,
                                                       strict, std::cout);
      else
        run<NodeOrder::HighIdFirst, EnableCoarseFilter>(jobs, capacity, slots,
                                                        strict, std::cout);
    } else {
      if (order == "low")
        run<NodeOrder::LowIdFirst>(jobs, capacity, slots, strict, std::cout);
      else
        run<NodeOrder::HighIdFirst>(jobs, capacity, slots, strict, std::cout);
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
