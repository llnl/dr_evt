/******************************************************************************
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
/** @file placement_driver.cpp
 * @brief Validation-only queue traversal using the reusable placement model.
 * @details Whole nodes, equal priority, unlimited queue examination, no release
 * delay or dispatch latency. This is not a simulator scheduler implementation.
 */
#define DR_EVT_HAS_CONFIG 1
#include "sim/resource/placement_model.hpp"
#include <iomanip>
#include <iostream>
#include <ranges>
#include <string>

using namespace dr_evt;

/** @brief One stable job owner through submission, execution, and release. */
struct Job {
  sim_time_t submit;     ///< Submission time.
  num_nodes_t quantity;  ///< Requested whole-node quantity.
  tdiff_t limit;         ///< Estimated runtime used in planning.
  tdiff_t runtime;       ///< Actual runtime used for completion.
  bool launched = false; ///< Whether the job has ever started.
  sim_time_t finish = 0; ///< Actual completion time, meaningful after launch.
  BitmapAllocation allocation; ///< Live identities; cleared on completion.
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
  using Model = PlacementModel<NoResourceObserver, Order, CoarseFilter>;
  Bitmap occupied(capacity);
  const Bitmap inventory(capacity, true);
  sim_time_t time = jobs.empty() ? 0 : jobs.front().submit;
  size_t completed = 0;
  while (completed < jobs.size()) {
    for (auto &job : jobs) {
      if (job.launched && job.finish == time &&
          job.allocation.capacity() != 0) {
        Model::release(occupied, job.allocation);
        job.allocation = Bitmap{};
        ++completed;
      }
    }
    // This planner dies before any job-owned allocation changes at the next
    // event.
    std::optional<typename Model::Timeline> plan;
    const auto ensure_plan = [&]() {
      if (plan)
        return;
      plan.emplace(inventory, time);
      auto live = jobs | std::views::filter([time](const Job &job) {
                    return job.launched && job.finish > time;
                  });
      plan->seed_running(live, [&](const Job &job) {
        return typename Model::Timeline::Interval{
            time, job.finish + job.limit - job.runtime,
            static_cast<job_no_t>(&job - jobs.data()),
            std::cref(job.allocation)};
      });
    };
    size_t reserved = 0;
    for (size_t index = 0; index < jobs.size(); ++index) {
      auto &job = jobs[index];
      if (job.launched || job.submit > time)
        continue;
      std::optional<typename Model::Placement> placement;
      if (!plan)
        placement = Model::find_available(inventory, occupied, job.quantity,
                                          job.limit, time);
      if (!placement && (plan || reserved < slots)) {
        ensure_plan();
        placement = reserved < slots
                        ? Model::find_fit(*plan, job.quantity, job.limit, time)
                        : Model::find_at(*plan, job.quantity, job.limit, time);
      }
      if (!placement) {
        if (strict)
          break;
        continue;
      }
      if (placement->start > time) {
        Model::reserve(*plan, std::move(*placement), job.quantity, job.limit,
                       {index, time});
        ++reserved;
        continue;
      }
      Model::allocate(occupied, placement->allocation);
      job.allocation = std::move(placement->allocation);
      job.launched = true;
      job.finish = time + job.runtime;
      if (plan)
        plan->borrow_running(job.allocation, time + job.limit, index);
      out << "{\"schedule\":{\"job\":" << index << ",\"start\":" << time
          << ",\"finish\":" << job.finish << ",\"node_ids\":";
      write_ids(job.allocation, out);
      out << "}}\n";
    }
    out << "{\"snapshot\":{\"time\":" << time << ",\"allocations\":[";
    bool first = true;
    for (size_t index = 0; index < jobs.size(); ++index) {
      const auto &job = jobs[index];
      if (!job.launched || job.finish <= time)
        continue;
      if (!first)
        out << ',';
      first = false;
      out << "{\"job\":" << index << ",\"node_ids\":";
      write_ids(job.allocation, out);
      out << '}';
    }
    out << "],\"reservations\":[";
    first = true;
    // Queue order, independent of the planner's temporal storage order.
    for (size_t index = 0; index < jobs.size(); ++index) {
      for (const auto &interval :
           plan ? plan->intervals()
                : std::span<const typename Model::Timeline::Interval>{}) {
        if (interval.job != index || interval.start <= time)
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
    for (const auto &job : jobs) {
      if (!job.launched && job.submit > time)
        next = std::min(next, job.submit);
      if (job.launched && job.finish > time)
        next = std::min(next, job.finish);
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
