/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

// epoch_t subtraction in the configured DR_EVT_INTEGER_TIME_DIFF mode.

#include "trace/epoch.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

namespace {

using dr_evt::epoch_t;
using dr_evt::tdiff_t;
// epoch_t is a std::pair, so argument-dependent lookup cannot find this.
using dr_evt::operator-;
using limits = std::numeric_limits<time_t>;

/** @brief Compare two doubles bit for bit, so signed zeros also match.
 * @param[in] a One value.
 * @param[in] b Other value.
 * @return true when both have the same bit pattern. */
bool same_bits(tdiff_t a, tdiff_t b) {
  return std::memcmp(&a, &b, sizeof a) == 0;
}

/** @brief Whether t1 - t2 fits in time_t.
 * @param[in] t1 Minuend.
 * @param[in] t2 Subtrahend.
 * @return true when the exact difference is representable. */
bool fits(time_t t1, time_t t2) {
  return t2 < 0 ? t1 <= limits::max() + t2 : t1 >= limits::min() + t2;
}

} // namespace

int main() {
  // Whenever the difference fits, both modes equal std::difftime exactly.
  std::vector<time_t> values{0,
                             1,
                             -1,
                             1597836579,
                             -1597836579,
                             4102444800,
                             (time_t{1} << 32) - 1,
                             time_t{1} << 32,
                             -(time_t{1} << 32),
                             (time_t{1} << 53) + 1,
                             -(time_t{1} << 53) - 1,
                             limits::max(),
                             limits::max() - 1,
                             limits::min(),
                             limits::min() + 1};
  std::mt19937_64 random(20261115);
  for (int i = 0; i < 200; ++i)
    values.push_back(static_cast<time_t>(random()));
  for (int i = 0; i < 200; ++i)
    values.push_back(static_cast<time_t>(random() % 4000000000ULL));
  size_t overflows = 0;
  for (const auto t1 : values)
    for (const auto t2 : values) {
      const epoch_t a{t1, 0.25f}, b{t2, 0.75f};
      const tdiff_t got = a - b;
      const tdiff_t legacy = std::difftime(t1, t2) + (a.second - b.second);
      if (fits(t1, t2)) {
        assert(same_bits(got, legacy));
        continue;
      }
      ++overflows;
#if DR_EVT_INTEGER_TIME_DIFF
      // Out of range: clamped to the time_t limit with t1's sign.
      const time_t limit = t1 < 0 ? limits::min() : limits::max();
      assert(
          same_bits(got, static_cast<tdiff_t>(limit) + (a.second - b.second)));
#else
      assert(same_bits(got, legacy));
#endif
    }
  assert(overflows != 0);

  // The unscheduled sentinel against real timestamps always fits.
  const epoch_t sentinel{limits::max(), 0.0f}, real{1597836579, 0.5f};
  assert(same_bits(sentinel - real,
                   std::difftime(sentinel.first, real.first) - 0.5));
  assert(same_bits(real - sentinel,
                   std::difftime(real.first, sentinel.first) + 0.5));
  assert(sentinel - sentinel == 0.0);

  std::cout << "epoch_t subtraction checks passed (DR_EVT_INTEGER_TIME_DIFF="
            << DR_EVT_INTEGER_TIME_DIFF << ")\n";
  return 0;
}
