/** @file queries.cpp
 * @brief Focused earliest-fit and boolean-query trials with 63 running
 * intervals. */
#define DR_EVT_HAS_CONFIG 1
#include "sim/resource/placement_model.hpp"
#include <chrono>
#include <iostream>
using namespace dr_evt;
/** @brief Time 20,000 queries against an unchanged timeline.
 * @param[in] argc Argument count.
 * @param[in] argv earliest selects earliest-fit; boolean selects feasibility.
 * @return Zero after emitting timings and hash; nonzero if a required fit
 * fails. */
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  const bool fit = std::string(argv[1]) == "earliest";
  using Model = PlacementModel<>;
  std::vector<Bitmap> allocations;
  for (unsigned g = 0; g < 63; ++g) {
    std::vector<num_nodes_t> ids;
    for (unsigned n = 0; n < 7; ++n)
      ids.push_back(g * 7 + n);
    allocations.push_back(Bitmap::from_ids(500, ids));
  }
  Model::Timeline plan(Bitmap(500, true), 0);
  for (unsigned g = 0; g < 63; ++g)
    plan.borrow_running(allocations[g], g + 1, g);
  size_t hash = 0;
  auto begin = std::chrono::steady_clock::now();
  for (unsigned iteration = 0; iteration < 20000; ++iteration) {
    if (fit) {
      auto result = Model::find_fit(plan, 256, 30, iteration % 4);
      if (!result)
        return 2;
      hash += static_cast<size_t>(result->start) + result->allocation.count();
    } else {
      hash += Model::can_fit(plan, 256, 30, iteration % 64);
    }
  }
  std::cout << "{\"ms\":"
            << std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - begin)
                   .count()
            << ",\"hash\":" << hash << "}\n";
}
