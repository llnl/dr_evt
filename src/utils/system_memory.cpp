/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file system_memory.cpp
 * @brief Linux available-memory query and deterministic test override.
 */

#include "utils/system_memory.hpp"
#include <cstdlib> // std::getenv
#include <fstream>
#include <sstream>
#include <string>

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *    @{ */

namespace {

/**
 * @brief Parse a complete non-negative integer string.
 * @param[in] str Text to parse.
 * @param[out] out Destination updated only on successful parsing.
 * @return true if str contains only a representable non-negative integer;
 *         false otherwise, leaving out unchanged.
 */
bool parse_nonneg(const std::string &str, std::size_t &out) {
  if (str.empty()) {
    return false;
  }
  try {
    std::size_t pos = 0;
    unsigned long long v = std::stoull(str, &pos);
    if (pos != str.size()) {
      return false; // trailing junk (e.g. a unit suffix) - reject rather than
                    // guess
    }
    out = static_cast<std::size_t>(v);
    return true;
  } catch (...) {
    return false;
  }
}

} // anonymous namespace

std::size_t get_available_memory_bytes() {
  // Test seam: see this function's doc comment in system_memory.hpp.
  if (const char *override_str =
          std::getenv("DR_EVT_TEST_AVAILABLE_MEMORY_BYTES")) {
    std::size_t bytes = 0;
    if (parse_nonneg(override_str, bytes)) {
      return bytes;
    }
    // Malformed override: fall through to the real query rather than
    // silently ignoring what looks like a deliberate test setting.
  }

#if defined(__linux__)
  std::ifstream meminfo("/proc/meminfo");
  if (!meminfo) {
    return 0;
  }
  std::string line;
  while (std::getline(meminfo, line)) {
    static const std::string key = "MemAvailable:";
    if (line.compare(0, key.size(), key) != 0) {
      continue;
    }
    // Format: "MemAvailable:    1234567 kB" - extract just the number.
    std::istringstream iss(line.substr(key.size()));
    std::size_t kb = 0;
    std::string unit;
    if (!(iss >> kb)) {
      return 0;
    }
    iss >> unit; // "kB" - /proc/meminfo has only ever used this unit
    return kb * 1024;
  }
  // No MemAvailable line - kernel older than 3.14. Not worth falling
  // back to MemFree (which understates availability by not counting
  // reclaimable caches, the exact undercount MemAvailable exists to
  // fix); better to report "unknown" than mislead a caller into
  // refusing work that would actually have fit.
  return 0;
#else
  return 0;
#endif
}

/**@}*/
} // end of namespace dr_evt
