/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file bitmap.hpp
 * @brief Dense, word-parallel resource membership and allocation masks.
 */
#ifndef DR_EVT_SIM_RESOURCE_BITMAP_HPP
#define DR_EVT_SIM_RESOURCE_BITMAP_HPP

#include "dr_evt_types.hpp"
#include <algorithm>
#include <bit>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dr_evt {

/// Compile-time ordering for selecting interchangeable resource identities.
enum class NodeOrder { LowIdFirst, HighIdFirst };

/**
 * @brief Owning dense bitmap with contiguous 64-bit resource words.
 * @details One denotes membership; the caller decides whether membership means
 * eligible, available, allocated, or releasing. Padding bits are always zero.
 * IDs range from zero through capacity()-1. No count or expanded IDs are
 * cached.
 */
class Bitmap {
  num_nodes_t m_capacity = 0;
  std::vector<std::uint64_t> m_words;

  /** @brief Return the valid final-word mask.
   * @return Mask excluding resource IDs beyond the domain. */
  std::uint64_t tail_mask() const noexcept {
    const auto tail = m_capacity % 64;
    return tail ? (std::uint64_t{1} << tail) - 1 : ~std::uint64_t{0};
  }

  /** @brief Validate domains before accessing or modifying words.
   * @param[in] other Bitmap to combine with this bitmap.
   * @throws std::invalid_argument if domain sizes differ. */
  void same_domain(const Bitmap &other) const {
    if (m_capacity != other.m_capacity) {
      throw std::invalid_argument("bitmap resource domains differ");
    }
  }

  /** @brief Select part of a nonempty word without modifying it.
   * @tparam Order Compile-time low-ID or high-ID selection.
   * @param[in] bits Available membership bits.
   * @param[in] quantity Positive quantity smaller than the word population.
   * @return Selected mask.
   * @details Full words use a shift; sparse partial words use bit scans and
   * clearing. Complete words bypass this helper and are selected in bulk. */
  template <NodeOrder Order>
  static std::uint64_t partial_word(std::uint64_t bits,
                                    size_t quantity) noexcept {
    if (bits == ~std::uint64_t{0}) {
      if constexpr (Order == NodeOrder::LowIdFirst) {
        return (~std::uint64_t{0}) >> (64 - quantity);
      } else {
        return (~std::uint64_t{0}) << (64 - quantity);
      }
    }
    std::uint64_t result = 0;
    for (size_t i = 0; i < quantity; ++i) {
      if constexpr (Order == NodeOrder::LowIdFirst) {
        const auto bit = bits & (0 - bits);
        result |= bit;
        bits &= bits - 1;
      } else {
        const auto bit = std::uint64_t{1} << (63 - std::countl_zero(bits));
        result |= bit;
        bits &= ~bit;
      }
    }
    return result;
  }

public:
  /** @brief Construct an empty resource domain. */
  Bitmap() = default;

  /** @brief Construct a fixed resource domain.
   * @param[in] capacity Number of resource identities in the domain.
   * @param[in] full Whether all valid resources initially belong to the set.
   * @throws std::bad_alloc if word storage cannot be allocated. */
  explicit Bitmap(num_nodes_t capacity, bool full = false)
      : m_capacity(capacity),
        m_words(static_cast<size_t>(capacity) / 64 + (capacity % 64 != 0),
                full ? ~std::uint64_t{0} : 0) {
    if (!m_words.empty()) {
      m_words.back() &= tail_mask();
    }
  }

  /** @brief Copy owned resource membership.
   * @param[in] other Bitmap to copy.
   * @throws std::bad_alloc if word storage cannot be allocated. */
  Bitmap(const Bitmap &other) = default;

  /** @brief Transfer membership and leave the source as an empty domain.
   * @param[in,out] other Bitmap whose word storage transfers. */
  Bitmap(Bitmap &&other) noexcept
      : m_capacity(std::exchange(other.m_capacity, 0)),
        m_words(std::move(other.m_words)) {}

  /** @brief Replace membership with an independent copy.
   * @param[in] other Bitmap to copy.
   * @return This bitmap after assignment.
   * @throws std::bad_alloc if word storage cannot be allocated.
   * @details Existing word capacity is reused when sufficient. With integral
   * words, allocation failure leaves the original membership unchanged. */
  Bitmap &operator=(const Bitmap &other) {
    if (this != &other) {
      m_words = other.m_words;
      m_capacity = other.m_capacity;
    }
    return *this;
  }

  /** @brief Transfer membership and leave the source as an empty domain.
   * @param[in,out] other Bitmap whose word storage transfers; self-move is
   * valid.
   * @return This bitmap after assignment. */
  Bitmap &operator=(Bitmap &&other) noexcept {
    if (this != &other) {
      m_capacity = std::exchange(other.m_capacity, 0);
      m_words = std::move(other.m_words);
    }
    return *this;
  }

  /** @brief Construct resource membership from borrowed identities.
   * @param[in] capacity Size of the resource domain.
   * @param[in] ids Resource IDs; input need not be ordered and is not retained.
   * @return Owning bitmap of exactly the supplied identities.
   * @throws std::out_of_range if an ID is outside the domain.
   * @throws std::invalid_argument if an ID appears more than once.
   * @throws std::bad_alloc if bitmap storage cannot be allocated. */
  static Bitmap from_ids(num_nodes_t capacity,
                         std::span<const num_nodes_t> ids) {
    Bitmap result(capacity);
    for (auto id : ids) {
      if (result.test(id)) {
        throw std::invalid_argument("duplicate resource ID");
      }
      result.m_words[id / 64] |= std::uint64_t{1} << (id % 64);
    }
    return result;
  }

  /** @brief Return the resource domain size.
   * @return Number of valid IDs, including absent members. */
  num_nodes_t capacity() const noexcept { return m_capacity; }

  /** @brief Borrow contiguous membership words.
   * @return Read-only view valid until assignment, move, or destruction. */
  std::span<const std::uint64_t> words() const noexcept { return m_words; }

  /** @brief Count membership using population-count operations.
   * @return Number of set valid bits. */
  size_t count() const noexcept {
    size_t result = 0;
    for (auto word : m_words) {
      result += std::popcount(word);
    }
    return result;
  }

  /** @brief Test one resource's membership.
   * @param[in] id Resource identity.
   * @return true when its membership bit is set.
   * @throws std::out_of_range if id is outside the domain. */
  bool test(num_nodes_t id) const {
    if (id >= m_capacity) {
      throw std::out_of_range("resource ID outside bitmap domain");
    }
    return (m_words[id / 64] & (std::uint64_t{1} << (id % 64))) != 0;
  }

  /** @brief Expand membership for output or interoperability.
   * @return Newly owned ascending IDs; no list is retained by this bitmap.
   * @throws std::bad_alloc if output storage cannot be allocated. */
  std::vector<num_nodes_t> ids() const {
    std::vector<num_nodes_t> result;
    result.reserve(count());
    for (size_t index = 0; index < m_words.size(); ++index) {
      auto bits = m_words[index];
      while (bits != 0) {
        result.push_back(
            static_cast<num_nodes_t>(index * 64 + std::countr_zero(bits)));
        bits &= bits - 1;
      }
    }
    return result;
  }

  /** @brief Test complete membership of another bitmap.
   * @param[in] other Membership to test in the same domain.
   * @return true when all of other's bits belong to this set.
   * @throws std::invalid_argument if domains differ. */
  bool contains(const Bitmap &other) const {
    same_domain(other);
    for (size_t i = 0; i < m_words.size(); ++i) {
      if ((m_words[i] & other.m_words[i]) != other.m_words[i]) {
        return false;
      }
    }
    return true;
  }

  /** @brief Test any shared membership.
   * @param[in] other Membership to test in the same domain.
   * @return true when any selected resource is shared.
   * @throws std::invalid_argument if domains differ. */
  bool intersects(const Bitmap &other) const {
    same_domain(other);
    for (size_t i = 0; i < m_words.size(); ++i) {
      if (m_words[i] & other.m_words[i]) {
        return true;
      }
    }
    return false;
  }

  /** @brief Union membership with contiguous word-wise OR operations.
   * @param[in] other Membership to add; existing members remain set.
   * @throws std::invalid_argument if domains differ.
   * @details Domain validation precedes mutation; self-union is valid. */
  void add(const Bitmap &other) {
    same_domain(other);
    for (size_t i = 0; i < m_words.size(); ++i) {
      m_words[i] |= other.m_words[i];
    }
  }

  /** @brief Clear membership using contiguous word-wise AND-NOT operations.
   * @param[in] other Membership to remove; absent members remain clear.
   * @throws std::invalid_argument if domains differ.
   * @details Domain validation precedes mutation. This is idempotent. */
  void remove(const Bitmap &other) {
    same_domain(other);
    for (size_t i = 0; i < m_words.size(); ++i) {
      m_words[i] &= ~other.m_words[i];
    }
  }

  /** @brief Toggle membership with contiguous word-wise XOR operations.
   * @param[in] other Membership to invert.
   * @throws std::invalid_argument if domains differ.
   * @details Domain validation precedes mutation. This primitive is not a
   * committed allocation or release operation. */
  void toggle(const Bitmap &other) {
    same_domain(other);
    for (size_t i = 0; i < m_words.size(); ++i) {
      m_words[i] ^= other.m_words[i];
    }
  }

  /** @brief Intersect membership with a bitmap in the same domain.
   * @param[in] other Bitmap to intersect; self-intersection is valid.
   * @throws std::invalid_argument if domains differ. */
  void intersect_with(const Bitmap &other) {
    same_domain(other);
    intersect_words(other.words());
  }

  /** @brief Intersect with a borrowed dense filter row.
   * @param[in] row Contiguous words with exactly this bitmap's word count.
   * @throws std::invalid_argument if word counts differ.
   * @details The row is not retained. AND cannot introduce padding bits. */
  void intersect_words(std::span<const std::uint64_t> row) {
    if (row.size() != m_words.size()) {
      throw std::invalid_argument("bitmap word counts differ");
    }
    for (size_t i = 0; i < m_words.size(); ++i) {
      m_words[i] &= row[i];
    }
  }

  /** @brief Select N members without modifying source membership.
   * @tparam Order Compile-time identity order; result words remain ascending.
   * @param[in] quantity Number requested; zero selects an empty set.
   * @return Complete owning allocation, or std::nullopt when insufficient.
   * @throws std::bad_alloc if result word storage cannot be allocated.
   * @details Whole words are selected together. Only the final partial word
   * needs bit selection. Failure leaves the source unchanged, without rollback.
   */
  template <NodeOrder Order = NodeOrder::LowIdFirst>
  std::optional<Bitmap> select(num_nodes_t quantity) const {
    if (quantity > m_capacity) {
      return std::nullopt;
    }
    Bitmap selected(m_capacity);
    size_t remaining = quantity;
    for (size_t cursor = 0; cursor < m_words.size() && remaining; ++cursor) {
      const size_t index =
          Order == NodeOrder::LowIdFirst ? cursor : m_words.size() - cursor - 1;
      const auto bits = m_words[index];
      const auto available = static_cast<size_t>(std::popcount(bits));
      const size_t taken = std::min(remaining, available);
      selected.m_words[index] =
          taken == available ? bits : partial_word<Order>(bits, taken);
      remaining -= taken;
    }
    if (remaining) {
      return std::nullopt;
    }
    return selected;
  }

  /** @brief Retain the first N members in an already-owned scratch bitmap.
   * @tparam Order Compile-time selection order.
   * @param[in] quantity Members to keep; zero clears membership.
   * @return true after complete selection; false when insufficient.
   * @details Feasibility precedes mutation, so failure leaves membership
   * unchanged. Selection reuses word storage and performs no allocation. */
  template <NodeOrder Order = NodeOrder::LowIdFirst>
  bool retain_first(num_nodes_t quantity) noexcept {
    size_t available = 0;
    for (size_t cursor = 0; cursor < m_words.size() && available < quantity;
         ++cursor) {
      const auto index =
          Order == NodeOrder::LowIdFirst ? cursor : m_words.size() - cursor - 1;
      available += std::popcount(m_words[index]);
    }
    if (available < quantity)
      return false;
    size_t remaining = quantity;
    for (size_t cursor = 0; cursor < m_words.size(); ++cursor) {
      const auto index =
          Order == NodeOrder::LowIdFirst ? cursor : m_words.size() - cursor - 1;
      if (!remaining) {
        m_words[index] = 0;
        continue;
      }
      const auto bits = m_words[index];
      const auto population = static_cast<size_t>(std::popcount(bits));
      const auto taken = std::min(remaining, population);
      m_words[index] =
          taken == population ? bits : partial_word<Order>(bits, taken);
      remaining -= taken;
    }
    return true;
  }

  /** @brief Select eligible members excluding a borrowed blocked mask.
   * @tparam Order Compile-time selection order.
   * @param[in] blocked Unavailable identities in the same domain; not retained.
   * @param[in] quantity Requested members; zero selects an empty set.
   * @return Owning selection or std::nullopt when insufficient.
   * @throws std::invalid_argument if domains differ.
   * @throws std::bad_alloc if result storage cannot be allocated.
   * @details Sources are unchanged; no intermediate exclusion mask is
   * allocated. */
  template <NodeOrder Order = NodeOrder::LowIdFirst>
  std::optional<Bitmap> select_excluding(const Bitmap &blocked,
                                         num_nodes_t quantity) const {
    same_domain(blocked);
    if (quantity > m_capacity)
      return std::nullopt;
    Bitmap selected(m_capacity);
    size_t remaining = quantity;
    for (size_t cursor = 0; cursor < m_words.size() && remaining; ++cursor) {
      const size_t index =
          Order == NodeOrder::LowIdFirst ? cursor : m_words.size() - cursor - 1;
      const auto bits = m_words[index] & ~blocked.m_words[index];
      const auto available = static_cast<size_t>(std::popcount(bits));
      const auto taken = std::min(remaining, available);
      selected.m_words[index] =
          taken == available ? bits : partial_word<Order>(bits, taken);
      remaining -= taken;
    }
    if (remaining)
      return std::nullopt;
    return selected;
  }

  /** @brief Compare resource domains and membership.
   * @param[in] other Bitmap to compare.
   * @return true when domains and every membership word match. */
  bool operator==(const Bitmap &other) const = default;
};

/// An allocation owns a dense bitmap; it adds no second membership
/// representation.
using BitmapAllocation = Bitmap;

} // namespace dr_evt
#endif
