/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_UTILS_RNGEN_HPP
#define DR_EVT_UTILS_RNGEN_HPP

#include <chrono>
#include <memory>
#include <random>

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#if defined(DR_EVT_HAS_CEREAL)
#include "utils/state_io_cereal.hpp"
#include <cereal/archives/binary.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/vector.hpp>
ENABLE_CUSTOM_CEREAL(std::minstd_rand);
ENABLE_CUSTOM_CEREAL(std::minstd_rand0);
ENABLE_CUSTOM_CEREAL(std::mt19937)
ENABLE_CUSTOM_CEREAL(std::mt19937_64)
ENABLE_CUSTOM_CEREAL(std::uniform_int_distribution<unsigned long long>)
ENABLE_CUSTOM_CEREAL(std::uniform_int_distribution<long long>)
ENABLE_CUSTOM_CEREAL(std::uniform_int_distribution<uint32_t>)
ENABLE_CUSTOM_CEREAL(std::uniform_int_distribution<int>)
ENABLE_CUSTOM_CEREAL(std::uniform_real_distribution<double>)
ENABLE_CUSTOM_CEREAL(std::uniform_real_distribution<float>)
#endif // DR_EVT_HAS_CEREAL

#include "utils/seed.hpp"
#include "utils/state_io.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

/**
 * @brief Reproducible random-number source with serializable engine state.
 *
 * @details RNGen couples a Mersenne-Twister engine to a standard-library
 * distribution.  When DR_EVT_THREAD_PRIVATE_RNG is enabled, it owns one engine
 * per OpenMP thread while sharing the distribution parameters.  Saving the
 * complete state preserves the next value produced after a restart.
 *
 * @tparam D Standard-library-compatible distribution template.
 * @tparam V Value returned by the distribution and by operator()().
 */
template <template <typename> typename D = std::uniform_real_distribution,
          typename V = double>
class RNGen {
public:
  using result_type = V;       ///< Numeric type produced by each draw.
  using distribution_t = D<V>; ///< Distribution applied to engine output.
  using param_type =
      typename distribution_t::param_type; ///< Distribution parameter bundle.
  using generator_type =
      std::mt19937; ///< Engine type; selected for statistical quality.
  // using generator_type = std::minstd_rand;
#if DR_EVT_THREAD_PRIVATE_RNG
  using generator_list_t = std::vector<std::unique_ptr<generator_type>>;
#endif // DR_EVT_THREAD_PRIVATE_RNG

  /** @brief Construct an engine with its default seed and distribution. */
  RNGen();

  /** @brief Reinitialize every engine from one deterministic seed.
   * @param[in] s Seed value recorded with serialized state. */
  void set_seed(unsigned s);
  /** @brief Reinitialize every engine from a clock-derived seed. */
  void set_seed();
  /**
   * @brief Seed every engine from a sequence of input words.
   *
   * Set seed_seq input to generate a seed_seq object such that a sequence of
   * values (as long as the state size) rather than a single value can be used
   * for seeding
   * @param[in] p Seed-sequence material copied into this generator.
   */
  void use_seed_seq(const seed_seq_param_t &p);
  /** @brief Replace the distribution parameters used for future draws.
   * @param[in] p Parameter bundle accepted by distribution_t. */
  void param(const param_type &p);
  /** @brief Return the current distribution parameters.
   * @return param_type value copied from the configured distribution. */
  param_type param() const;
  /** @brief Draw a value using the generator for the calling thread.
   * @return result_type variate from the configured distribution. */
  result_type operator()();
  /**
   * @brief Draw a value from engine zero.
   *
   * This is similar to operator() in that it returns a random value drawn from
   * the current distribution. However, the difference comes from nested
   * parallel regions. The `operator()` returns a value from the thread private
   * generator identified the id of a caller thread. When the caller is not
   * a worker thread at the inner level, but the parent thread at the outer
   * level, the thread id is no longer relevant. Thefore, we pull a value from
   * the first generator object.
   * @return result_type variate drawn from engine zero.
   */
  result_type pull();
  /** @brief Return the configured random-value distribution.
   * @return Read-only reference to distribution_t. */
  const distribution_t &distribution() const;
  /** @brief Return the generator state length in engine words.
   * @return Compile-time word count as unsigned. */
  static constexpr unsigned get_state_size();

#if DR_EVT_THREAD_PRIVATE_RNG
  /** @brief Expose the thread-private engines for state inspection.
   * @return Mutable reference to generator_list_t. */
  generator_list_t &engine();
  /** @brief Expose the thread-private engines without allowing mutation.
   * @return Read-only reference to generator_list_t. */
  const generator_list_t &engine() const;
#else
  /** @brief Expose the shared engine for state inspection.
   * @return Mutable reference to generator_type. */
  generator_type &engine();
  /** @brief Expose the shared engine without allowing mutation.
   * @return Read-only reference to generator_type. */
  const generator_type &engine() const;
#endif // DR_EVT_THREAD_PRIVATE_RNG

#if defined(DR_EVT_HAS_CEREAL)
  /** @brief Serialize or deserialize the complete generator state.
   * @tparam Archive Cereal archive type.
   * @param[in,out] ar Archive receiving or supplying the state. */
  template <class Archive> void serialize(Archive &ar) {
    ar(m_seed, m_sseq_used, m_sseq_param, m_gen, m_distribution);
    // ar(m_seed, m_sseq_used, m_gen, m_distribution);
  }
  friend class cereal::access;
#endif // defined(DR_EVT_HAS_CEREAL)

  /** @brief Check whether a binary stream has compatible element widths.
   * @tparam S Binary stream type exposing char_type.
   * @param[in] stream Stream whose storage representation is inspected.
   * @return bool indicating whether raw state words can be transferred. */
  template <typename S> static bool check_bits_compatibility(const S &stream);
  /** @brief Serialize seed metadata, engines, and distribution state.
   * @tparam S Writable binary stream type.
   * @param[in,out] os Stream receiving an architecture-dependent snapshot.
   * @return S& referring to @p os after the write. */
  template <typename S> S &save_bits(S &os) const;
  /** @brief Restore seed metadata, engines, and distribution state.
   * @tparam S Readable binary stream type.
   * @param[in,out] is Stream supplying a snapshot created by save_bits().
   * @return S& referring to @p is after the read. */
  template <typename S> S &load_bits(S &is);
  /** @brief Return the serialized size of the complete generator state.
   * @return Snapshot extent in bytes as size_t. */
  size_t byte_size() const;

  /** @brief Serialize only the underlying engine state.
   * @tparam S Writable binary stream type.
   * @param[in,out] os Destination stream.
   * @return S& referring to @p os after the write. */
  template <typename S> S &save_engine_bits(S &os) const;
  /** @brief Restore only the underlying engine state.
   * @tparam S Readable binary stream type.
   * @param[in,out] is Source stream.
   * @return S& referring to @p is after the read. */
  template <typename S> S &load_engine_bits(S &is);
  /** @brief Return the serialized size of the underlying engine state.
   * @return Engine snapshot extent in bytes as size_t. */
  size_t engine_byte_size() const;

#if DR_EVT_THREAD_PRIVATE_RNG
  /**
   * @brief Set the number of thread-private random engines.
   *
   * Set the number of omp threads to use. By default it is set to the value
   * returned by omp_get_max_threads(). If it has to be different, call this
   * function before calling `param()`.
   * @param[in] n Positive number of engine instances to create.
   */
  void set_num_threads(int n) { m_num_threads = n; }
  /** @brief Return the configured number of thread-private engines.
   * @return Engine count as int. */
  int get_num_threads() const { return m_num_threads; }
#endif // DR_EVT_THREAD_PRIVATE_RNG

protected:
  using n_threads_t = uint8_t; ///< Type used to store the OpenMP thread count.
  /**
   * seed value when a single seed value is used or the master seed
   * to generate a seed sequence
   */
  unsigned m_seed;
  /// Whether to use seed_seq
  bool m_sseq_used;
  /// seed_seq input
  seed_seq_param_t m_sseq_param;
#if DR_EVT_THREAD_PRIVATE_RNG
  /// Set of thread private generator objects identifiable by the thread id
  generator_list_t m_gen;
#else
  generator_type m_gen; ///< Generator used when generators are shared.
#endif // DR_EVT_THREAD_PRIVATE_RNG
  distribution_t
      m_distribution; ///< Distribution used to transform engine output.

#if DR_EVT_THREAD_PRIVATE_RNG
  int m_num_threads; ///< Number of OpenMP-indexed engines to allocate.
#endif               // DR_EVT_THREAD_PRIVATE_RNG
};

/**@}*/
} // namespace dr_evt

#include "rngen_impl.hpp"

#endif // DR_EVT_UTILS_RNGEN_HPP
