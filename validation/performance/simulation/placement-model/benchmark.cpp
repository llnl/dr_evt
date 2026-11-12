/** @file benchmark.cpp
 * @brief Shared queue engine for independent placement optimization trials.
 * @details Parsing, initial job construction, output, and final audits are
 * outside the timer. DR_EVT_PLACEMENT_TRIAL selects only the experiment's queue
 * behavior.
 */
#ifndef DR_EVT_PLACEMENT_TRIAL
#define DR_EVT_PLACEMENT_TRIAL 5
#endif
#define DR_EVT_HAS_CONFIG 1
#include "sim/resource/capacity_model.hpp"
#include "sim/resource/placement_model.hpp"
#include <bit>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>

using namespace dr_evt;
/** @brief Immutable trace demand and duration. */
struct Input {
  sim_time_t submit;
  num_nodes_t quantity;
  tdiff_t duration;
};
/** @brief Stable allocation owner through release.
 * @tparam Model Compile-time accounting and allocation type. */
template <class Model> struct Job {
  Input input;
  typename Model::Allocation allocation{};
  sim_time_t start = -1, finish = 0;
};
/** @brief Timings and decision hashes for one run. */
struct Result {
  double milliseconds = 0, makespan = 0;
  size_t completed = 0, passes = 0, queries = 0, future_reservations = 0;
  size_t max_pending = 0, max_running = 0;
  uint64_t schedule = 1469598103934665603ULL;
  uint64_t identities = 1469598103934665603ULL;
  uint64_t reservations = 1469598103934665603ULL;
};
/** @brief Fold an audit value into a deterministic hash.
 * @param[in,out] hash Running hash.
 * @param[in] value Decision or mask word to include. */
static void mix(uint64_t &hash, uint64_t value) {
  hash ^= value;
  hash *= 1099511628211ULL;
}

// A shared grouped-event queue engine. Jobs own allocations; the completion
// heap also supplies the running view. Pending pointers are compacted in place.
// Timed work includes resource accounting, plan construction, queue traversal,
// reservation updates, completion handling, and identical bookkeeping. Parsing,
// initial job-record construction, output, and the final audit are excluded.
/** @brief Run one placement or capacity simulation through the shared queue
 * engine.
 * @tparam Placement Whether allocations retain node identities.
 * @tparam Filter Compile-time coarse-filter choice.
 * @param[in] input Submission-ordered trace.
 * @param[in] capacity Node pool size.
 * @param[in] slots Future reservation depth.
 * @param[in] queue_depth Examination limit, or zero for unlimited.
 * @return Scheduling timings, counters, and audit hashes.
 * @throws std::runtime_error if progress, accounting, or the final audit fails.
 * @throws std::invalid_argument if planning or resource accounting is invalid.
 * @throws std::bad_alloc if queue, allocation, or planning storage cannot be
 * allocated. */
template <bool Placement, auto Filter = EnableCoarseFilter>
static Result simulate(const std::vector<Input> &input, num_nodes_t capacity,
                       size_t slots, size_t queue_depth) {
  using Model = std::conditional_t<
      Placement,
      PlacementModel<NoResourceObserver, NodeOrder::LowIdFirst, Filter>,
      CapacityModel<>>;
  std::vector<Job<Model>> jobs;
  jobs.reserve(input.size());
  for (const auto &row : input)
    jobs.push_back({row});
  Result result;
  const auto begin = std::chrono::steady_clock::now();
  typename Model::State occupied = [&] {
    if constexpr (Placement)
      return Bitmap(capacity);
    else
      return num_nodes_t{0};
  }();
  Bitmap inventory(capacity, true);
  std::vector<Job<Model> *> running, pending;
  size_t submitted = 0;
  auto now = input.front().submit;
  const auto later = [](const Job<Model> *a, const Job<Model> *b) {
    return a->finish > b->finish;
  };
  while (result.completed != jobs.size()) {
    while (!running.empty() && running.front()->finish <= now) {
      std::pop_heap(running.begin(), running.end(), later);
      auto *job = running.back();
      running.pop_back();
      Model::release(occupied, job->allocation);
      job->allocation = typename Model::Allocation{};
      ++result.completed;
    }
    while (submitted < jobs.size() && jobs[submitted].input.submit <= now)
      pending.push_back(&jobs[submitted++]);
    result.max_pending = std::max(result.max_pending, pending.size());
    if (!pending.empty()) {
      ++result.passes;
      auto make_plan = [&]() -> typename Model::Timeline {
        if constexpr (Placement) {
          typename Model::Timeline timeline(inventory, now);
          if constexpr (DR_EVT_PLACEMENT_TRIAL == 2 ||
                        DR_EVT_PLACEMENT_TRIAL == 5) {
            timeline.seed_running(running, [&](const auto *job) {
              return typename Model::Timeline::Interval{
                  now, job->finish, static_cast<job_no_t>(job - jobs.data()),
                  std::cref(job->allocation)};
            });
          } else {
            for (const auto *job : running)
              timeline.borrow_running(job->allocation, job->finish,
                                      static_cast<job_no_t>(job - jobs.data()));
          }
          return timeline;
        } else {
          std::vector<std::pair<sim_time_t, num_nodes_t>> segments;
          segments.reserve(running.size() + 1);
          segments.emplace_back(now, capacity - occupied);
          for (const auto *job : running)
            segments.emplace_back(job->finish, job->input.quantity);
          std::sort(segments.begin() + 1, segments.end());
          size_t write = 1;
          num_nodes_t free = segments.front().second;
          for (size_t read = 1; read < segments.size(); ++read) {
            const auto release = segments[read];
            free += release.second;
            if (segments[write - 1].first == release.first)
              segments[write - 1].second = free;
            else
              segments[write++] = {release.first, free};
          }
          segments.resize(write);
          return typename Model::Timeline(std::move(segments));
        }
      };
      std::optional<typename Model::Timeline> plan;
      if constexpr (!Placement || (DR_EVT_PLACEMENT_TRIAL != 1 &&
                                   DR_EVT_PLACEMENT_TRIAL != 5))
        plan.emplace(make_plan());
      size_t reserved = 0, kept = 0, examined = 0;
      for (auto *job : pending) {
        if (queue_depth && examined >= queue_depth) {
          pending[kept++] = job;
          continue;
        }
        ++examined;
        ++result.queries;
        auto placement = [&]() -> std::optional<typename Model::Placement> {
          if constexpr (Placement && (DR_EVT_PLACEMENT_TRIAL == 1 ||
                                      DR_EVT_PLACEMENT_TRIAL == 5)) {
            if (!plan) {
              auto immediate = Model::find_available(inventory, occupied,
                                                     job->input.quantity,
                                                     job->input.duration, now);
              if (immediate || reserved >= slots)
                return immediate;
              plan.emplace(make_plan());
            }
          }
          if (reserved < slots)
            return Model::find_fit(*plan, job->input.quantity,
                                   job->input.duration, now);
          if constexpr (Placement)
            return Model::find_at(*plan, job->input.quantity,
                                  job->input.duration, now);
          else
            return Model::can_fit(*plan, job->input.quantity,
                                  job->input.duration, now)
                       ? std::optional<sim_time_t>(now)
                       : std::nullopt;
        }();
        if (!placement) {
          pending[kept++] = job;
          continue;
        }
        const auto index = static_cast<job_no_t>(job - jobs.data());
        const auto at = Model::start_time(*placement);
        if (at > now) {
          mix(result.reservations, index);
          mix(result.reservations, std::bit_cast<uint64_t>(now));
          mix(result.reservations, std::bit_cast<uint64_t>(at));
          if constexpr (Placement)
            for (auto word : placement->allocation.words())
              mix(result.reservations, word);
          Model::reserve(*plan, std::move(*placement), job->input.quantity,
                         job->input.duration, {index, now});
          ++reserved;
          ++result.future_reservations;
          pending[kept++] = job;
          continue;
        }
        job->start = now;
        job->finish = now + job->input.duration;
        if constexpr (Placement)
          job->allocation = std::move(placement->allocation);
        else
          job->allocation = job->input.quantity;
        Model::allocate(occupied, job->allocation);
        if constexpr (Placement) {
          if (plan)
            plan->borrow_running(job->allocation, job->finish, index);
          mix(result.identities, index);
          for (auto word : job->allocation.words())
            mix(result.identities, word);
        } else {
          Model::reserve(*plan, *placement, job->input.quantity,
                         job->input.duration);
          if (occupied > capacity)
            throw std::runtime_error("capacity overallocated");
        }
        mix(result.schedule, index);
        mix(result.schedule, std::bit_cast<uint64_t>(now));
        mix(result.schedule, std::bit_cast<uint64_t>(job->finish));
        running.push_back(job);
        std::push_heap(running.begin(), running.end(), later);
      }
      pending.resize(kept);
    }
    result.max_running = std::max(result.max_running, running.size());
    result.makespan = now;
    if (result.completed == jobs.size())
      break;
    auto next = submitted < jobs.size()
                    ? jobs[submitted].input.submit
                    : std::numeric_limits<sim_time_t>::infinity();
    if (!running.empty())
      next = std::min(next, running.front()->finish);
    if (!std::isfinite(next) || next <= now)
      throw std::runtime_error("no scheduling progress");
    now = next;
  }
  result.milliseconds = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - begin)
                            .count();
  for (const auto &job : jobs)
    if (job.start < job.input.submit ||
        job.finish - job.start != job.input.duration)
      throw std::runtime_error("submission or duration audit failed");
  if constexpr (Placement) {
    if (occupied.count())
      throw std::runtime_error("identities not released");
  } else if (occupied)
    throw std::runtime_error("capacity not released");
  return result;
}
/** @brief Run a single benchmark scenario and write JSON.
 * @param[in] argc Argument count.
 * @param[in] argv Trace, node count, easy|hybrid, and capacity|exact|coarse.
 * @return Zero after successful simulation, one on invalid input or failure. */
int main(int argc, char **argv) {
  try {
    if (argc != 5)
      throw std::runtime_error("trace nodes easy|hybrid capacity|exact|coarse");
    std::ifstream stream(argv[1]);
    std::string line;
    if (!std::getline(stream, line))
      throw std::runtime_error("missing trace");
    std::vector<Input> input;
    const auto capacity = static_cast<num_nodes_t>(std::stoul(argv[2]));
    while (std::getline(stream, line)) {
      std::replace(line.begin(), line.end(), ',', ' ');
      std::istringstream row(line);
      Input job{};
      if (!(row >> job.submit >> job.quantity >> job.duration) ||
          !job.quantity || job.quantity > capacity || job.duration <= 0 ||
          (!input.empty() && job.submit < input.back().submit))
        throw std::runtime_error("invalid trace");
      input.push_back(job);
    }
    if (input.empty())
      throw std::runtime_error("empty trace");
    const std::string policy(argv[3]), mode(argv[4]);
    if (policy != "easy" && policy != "hybrid")
      throw std::runtime_error("unknown policy");
    const size_t slots = policy == "easy" ? 1 : 32;
    const size_t queue_depth = policy == "easy" ? 0 : 32;
    Result r;
    if (mode == "capacity")
      r = simulate<false>(input, capacity, slots, queue_depth);
    else if (mode == "exact")
      r = simulate<true, DisableCoarseFilter>(input, capacity, slots,
                                              queue_depth);
    else if (mode == "coarse")
      r = simulate<true>(input, capacity, slots, queue_depth);
    else
      throw std::runtime_error("unknown model");
    std::cout << std::setprecision(17) << "{\"model\":\"" << mode
              << "\",\"policy\":\"" << policy << "\",\"nodes\":" << capacity
              << ",\"jobs\":" << input.size()
              << ",\"scheduling_ms\":" << r.milliseconds
              << ",\"makespan\":" << r.makespan
              << ",\"completed\":" << r.completed << ",\"passes\":" << r.passes
              << ",\"fit_queries\":" << r.queries
              << ",\"future_reservations\":" << r.future_reservations
              << ",\"max_pending\":" << r.max_pending
              << ",\"max_running\":" << r.max_running << ",\"schedule_hash\":\""
              << r.schedule << "\",\"identities_hash\":\"" << r.identities
              << "\",\"reservations_hash\":\"" << r.reservations << "\"}\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
