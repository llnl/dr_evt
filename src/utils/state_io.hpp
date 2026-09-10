/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_UTILS_STATE_IO_HPP
#define DR_EVT_UTILS_STATE_IO_HPP
#include "streambuff.hpp"
#include "streamvec.hpp"
#include "traits.hpp"
#include <iostream>

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

/**
 * Override the stream operators only for trivially copyable types.
 * Users call `bits()` on the object of such a type to use the overriden
 * interfaces. Note that the function is only defined for trivially
 * copyable ones. Calling on an object of a wrong type would generate a
 * compiler error.
 * https://stackoverflow.com/questions/1559254/are-there-binary-memory-streams-in-c
 */
template <typename T> struct bits_t {
  using value_type =
      typename std::remove_reference<T>::type; ///< Unqualified payload type.
  T v; ///< Referenced object whose bytes are streamed.
};

/** @brief Wrap mutable scalar storage for byte-wise stream I/O.
 * @tparam T Trivially copyable, non-vector payload type.
 * @param[in,out] v Object whose bytes will be read or written.
 * @return bits_t<T&> retaining a reference to @p v. */
template <typename T>
typename std::enable_if<!is_vector<T>::value &&
                            std::is_trivially_copyable<T>::value,
                        bits_t<T &>>::type
bits(T &v) {
  return bits_t<T &>{v};
}

/** @brief Wrap immutable scalar storage for byte-wise output.
 * @tparam T Trivially copyable, non-vector payload type.
 * @param[in] v Object whose bytes will be written.
 * @return bits_t<const T&> retaining a read-only reference to @p v. */
template <typename T>
typename std::enable_if<!is_vector<T>::value &&
                            std::is_trivially_copyable<T>::value,
                        bits_t<const T &>>::type
bits(const T &v) {
  return bits_t<const T &>{v};
}

/** @brief Wrap mutable contiguous vector storage for byte-wise stream I/O.
 * @tparam T Vector whose element type is trivially copyable and not bool.
 * @param[in,out] v Vector whose size and elements will be transferred.
 * @return bits_t<T&> retaining a reference to @p v. */
template <typename T>
typename std::enable_if<
    is_vector<T>::value && !is_bool<typename T::value_type>::value &&
        std::is_trivially_copyable<typename T::value_type>::value,
    bits_t<T &>>::type
bits(T &v) {
  return bits_t<T &>{v};
}

/** @brief Wrap immutable contiguous vector storage for byte-wise output.
 * @tparam T Vector whose element type is trivially copyable and not bool.
 * @param[in] v Vector whose size and elements will be written.
 * @return bits_t<const T&> retaining a read-only reference to @p v. */
template <typename T>
inline typename std::enable_if<
    is_vector<T>::value && !is_bool<typename T::value_type>::value &&
        std::is_trivially_copyable<typename T::value_type>::value,
    bits_t<const T &>>::type
bits(const T &v) {
  return bits_t<const T &>{v};
}

/** @brief Write a wrapped object's binary representation.
 * @tparam S Output stream type. @tparam T Wrapped payload type.
 * @param[in,out] os Destination stream. @param[in] b Wrapped object.
 * @return S& referring to @p os. */
template <typename S, typename T> S &operator<<(S &os, const bits_t<T &> &b);

/** @brief Read a binary representation into a wrapped object.
 * @tparam S Input stream type. @tparam T Wrapped payload type.
 * @param[in,out] is Source stream. @param[out] b Wrapper referencing the
 * destination.
 * @return S& referring to @p is. */
template <typename S, typename T> S &operator>>(S &is, const bits_t<T &> &b);

template <typename ObjT, typename CharT = char,
          typename Traits = std::char_traits<CharT>>
/** @brief Serialize an object into a caller-owned byte vector.
 * @tparam ObjT Object implementing save_bits(). @tparam CharT Buffer element
 * type.
 * @tparam Traits Character traits used by the memory stream.
 * @param[in] obj Object to snapshot. @param[out] buffer Resized serialized
 * bytes.
 * @return bool indicating stream success. */
bool save_state(const ObjT &obj, std::vector<CharT> &buffer);

template <typename ObjT, typename CharT = char,
          typename Traits = std::char_traits<CharT>>
/** @brief Restore an object from a byte vector.
 * @tparam ObjT Object implementing load_bits(). @tparam CharT Buffer element
 * type.
 * @tparam Traits Character traits used by the memory stream.
 * @param[out] obj Object receiving restored state. @param[in] buffer Snapshot
 * bytes.
 * @return bool indicating stream success. */
bool load_state(ObjT &obj, const std::vector<CharT> &buffer);

template <typename ObjT, typename CharT = char,
          typename Traits = std::char_traits<CharT>>
/** @brief Serialize an object into a preallocated byte buffer.
 * @param[in] obj Object to snapshot. @param[out] buffer Storage of at least
 * obj.byte_size() bytes.
 * @return bool indicating stream success. */
bool save_state(const ObjT &obj, CharT *buffer);

template <typename ObjT, typename CharT = char,
          typename Traits = std::char_traits<CharT>>
/** @brief Restore an object from a preallocated byte buffer.
 * @param[out] obj Object receiving restored state. @param[in] buffer Complete
 * snapshot storage.
 * @return bool indicating stream success. */
bool load_state(ObjT &obj, const CharT *buffer);

/**@}*/
} // end of namespace dr_evt

#include "state_io_impl.hpp"

#endif // DR_EVT_UTILS_STATE_IO_HPP
