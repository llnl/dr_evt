/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_UTILS_TRAITS_HPP
#define DR_EVT_UTILS_TRAITS_HPP
#include <cstddef>
#include <concepts>
#include <type_traits>
#include <vector>

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

/// Detect if type T is a vector type
template <typename T> struct is_vector : public std::false_type {};

/// Detect if type T is a vector type
template <typename T, typename A>
struct is_vector<std::vector<T, A>> : public std::true_type {};

template <typename T>
inline constexpr bool is_vector_v =
    is_vector<std::remove_cvref_t<T>>::value;

/** One-byte character type suitable for raw binary stream storage. */
template <typename T>
concept binary_character =
    std::same_as<std::remove_cv_t<T>, char> ||
    std::same_as<std::remove_cv_t<T>, signed char> ||
    std::same_as<std::remove_cv_t<T>, unsigned char> ||
    std::same_as<std::remove_cv_t<T>, std::byte>;

/** Non-container value whose object representation may be copied as bytes. */
template <typename T>
concept raw_binary_scalar =
    !is_vector_v<T> && std::is_trivially_copyable_v<std::remove_cvref_t<T>>;

/** std::vector with elements whose representations may be copied as bytes. */
template <typename T>
concept raw_binary_vector =
    is_vector_v<T> &&
    (!std::same_as<typename std::remove_cvref_t<T>::value_type, bool>) &&
    std::is_trivially_copyable_v<
        typename std::remove_cvref_t<T>::value_type>;

/** Value supported by DR_EVT's raw binary state helpers. */
template <typename T>
concept raw_binary_serializable = raw_binary_scalar<T> || raw_binary_vector<T>;

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_UTILS_TRAITS_HPP
