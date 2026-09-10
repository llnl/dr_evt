/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_UTILS_STATE_IO_CEREAL_HPP
#define DR_EVT_UTILS_STATE_IO_CEREAL_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#if defined(DR_EVT_HAS_CEREAL)
#include "streambuff.hpp"
#include "streamvec.hpp"
#include "traits.hpp" // is_trivially_copyable
#include <cereal/archives/binary.hpp>
#include <iostream>

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

/**
 * @brief Test whether a type may use DR_EVT's raw-byte Cereal adapter.
 * @tparam T Candidate archive value type.
 * @return bool compile-time value; true only for non-arithmetic, trivially
 * copyable objects whose in-memory representation can be archived directly.
 * @details The resulting archive is representation-dependent and should only
 * be exchanged between ABI-compatible builds.
 */
template <typename T> constexpr bool is_custom_bin_cerealizable() {
  return (!std::is_arithmetic<T>::value &&
          std::is_trivially_copyable<T>::value);
}
} // end of namespace dr_evt

/**
 * @brief Register raw-binary Cereal save/load functions for one type.
 * @param[in] T Trivially copyable non-arithmetic type to register.
 * @details The generated functions preserve the exact object representation;
 * they do not provide endian, compiler, or library-version portability.
 */
#define ENABLE_CUSTOM_CEREAL(T)                                                \
  namespace cereal {                                                           \
  inline std::enable_if_t<dr_evt::is_custom_bin_cerealizable<T>(), void>       \
  CEREAL_SAVE_FUNCTION_NAME(BinaryOutputArchive &ar, T const &t) {             \
    ar.saveBinary(std::addressof(t), sizeof(t));                               \
  }                                                                            \
  inline std::enable_if_t<dr_evt::is_custom_bin_cerealizable<T>(), void>       \
  CEREAL_LOAD_FUNCTION_NAME(BinaryInputArchive &ar, T &t) {                    \
    ar.loadBinary(std::addressof(t), sizeof(t));                               \
  }                                                                            \
  }

namespace dr_evt {

/** @brief Save one Cereal-serializable object to a binary archive.
 * @tparam T Archived object type.
 * @param[in] state Object whose complete state is written.
 * @param[in,out] os Destination stream.
 * @details Archive destruction flushes buffered Cereal output before return. */
template <typename T> void save_state(const T &state, std::ostream &os) {
  // Create an output archive with the given stream
  cereal::BinaryOutputArchive oarchive(os);

  oarchive(state); // Write the data to the archive
                   // archive goes out of scope,
                   // ensuring all contents are flushed to the stream
}

/** @brief Restore one Cereal-serializable object from a binary archive.
 * @tparam T Archived object type.
 * @param[out] state Object populated from the archive.
 * @param[in,out] is Source stream positioned at an archive boundary. */
template <typename T> void load_state(T &state, std::istream &is) {
  // Create an input archive using the given stream
  cereal::BinaryInputArchive iarchive(is);

  iarchive(state); // Read the data from the archive
}

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_HAS_CEREAL

#endif // DR_EVT_UTILS_STATE_IO_CEREAL_HPP
