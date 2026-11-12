/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file benchmark_reservation_records.cpp
 * @brief Count allocations and time unlimited-Q reservation-only traversals.
 * @details This executable replaces ordinary new/delete for measurement only.
 * Queue setup, prediction-cache warmup, validation, and output are not timed.
 */
#define DR_EVT_HAS_CONFIG 1
#include "sim/scheduler_fcfs_pass_hybrid_circular.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>

static std::uint64_t allocation_calls = 0;
static std::uint64_t allocation_bytes = 0;

void *operator new(std::size_t size) {
  void *memory = std::malloc(size == 0 ? 1 : size);
  if (!memory) {
    throw std::bad_alloc();
  }
  ++allocation_calls;
  allocation_bytes += size;
  return memory;
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *memory) noexcept { ::operator delete(memory); }
void operator delete[](void *memory, std::size_t) noexcept {
  ::operator delete(memory);
}

class BenchmarkScheduler : public dr_evt::FCFSPassHybridCircularScheduler {
public:
  using FCFSPassHybridCircularScheduler::FCFSPassHybridCircularScheduler;
  void prepare() { begin_start_time_plan(0); }
  void validate(std::size_t jobs) const {
    if (m_estimated_starts.size() != jobs || last_pass_count() != 1 ||
        last_retry_stopped() || active_job_count() != jobs ||
        get_fcfs_reservation_time() != 10) {
      throw std::runtime_error("reservation-only traversal changed behavior");
    }
    for (std::size_t i = 0; i < jobs; ++i) {
      const auto &prediction = m_estimated_starts[i];
      if (!prediction.reserved || prediction.job_id != i + 1 ||
          prediction.start != 10 + static_cast<double>(i)) {
        throw std::runtime_error("reservation prediction changed");
      }
    }
  }
};

int main(int argc, char **argv) {
  const std::size_t jobs = argc > 1 ? std::stoull(argv[1]) : 20000;
  const std::size_t runs = argc > 2 ? std::stoull(argv[2]) : 9;
  if (jobs == 0 || runs == 0) {
    throw std::invalid_argument("jobs and runs must be positive");
  }
  BenchmarkScheduler scheduler(4, jobs, dr_evt::BackfillPolicy::CONSERVATIVE,
                               0, dr_evt::CircularOverflowPolicy::GROW, 0, 0);
  for (std::size_t i = 0; i < jobs; ++i) {
    scheduler.insert_job(i + 1, 0, 1, 4);
  }
  const dr_evt::running_jobs_t running{{0, {0, 10, 4}}};
  scheduler.prepare();
  if (!scheduler.schedule(0, running, 0).empty()) {
    throw std::runtime_error("busy machine started a job during warmup");
  }
  scheduler.validate(jobs);
  std::cout << "jobs,run,wall_ms,new_calls,new_requested_bytes\n";
  for (std::size_t run = 0; run < runs; ++run) {
    scheduler.prepare();
    const auto calls_before = allocation_calls;
    const auto bytes_before = allocation_bytes;
    const auto before = std::chrono::steady_clock::now();
    const auto starts = scheduler.schedule(0, running, 0);
    const auto after = std::chrono::steady_clock::now();
    const auto calls = allocation_calls - calls_before;
    const auto bytes = allocation_bytes - bytes_before;
    if (!starts.empty()) {
      throw std::runtime_error("busy machine started a job");
    }
    scheduler.validate(jobs);
    const auto elapsed =
        std::chrono::duration<double, std::milli>(after - before).count();
    std::cout << jobs << ',' << run << ',' << elapsed << ',' << calls << ','
              << bytes << '\n';
  }
}
