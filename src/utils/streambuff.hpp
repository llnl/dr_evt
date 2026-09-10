/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_UTILS_STREAMBUFF_HPP
#define DR_EVT_UTILS_STREAMBUFF_HPP

#include <streambuf>

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

/**
 * Wraps an existing buffer to use it as the internal buffer of streambuf,
 * which is then used to construct an object of basic_ostream (or one derived
 * from it). The ostream will use it as its internal streambuf.
 * Users must make sure that the external allocation outlives the object of
 * this type. The capacity of this streambuf is limited by the size of the
 * underlying buffer space, which is specified in the constructor.
 */
template <typename CharT, typename Traits = std::char_traits<CharT>>
class ostreambuff : public std::basic_streambuf<CharT, Traits> {
public:
  using char_type = typename std::basic_streambuf<
      CharT, Traits>::char_type; ///< Character type stored in the buffer.
  using traits_type =
      typename std::basic_streambuf<CharT,
                                    Traits>::traits_type; ///< Character-traits
                                                          ///< type.
  using int_type = typename std::basic_streambuf<
      CharT, Traits>::int_type; ///< Traits-compatible integer character type.
  using pos_type =
      typename std::basic_streambuf<CharT,
                                    Traits>::pos_type; ///< Stream-position
                                                       ///< type.
  using off_type =
      typename std::basic_streambuf<CharT,
                                    Traits>::off_type; ///< Stream-offset type.

  ostreambuff() = delete; ///< Default construction is disabled.

  /**
   * @brief Bind an output stream buffer to caller-owned storage.
   *
   * Set the internal buffer with the buffer of the given vector, instead of
   * relying on setbuf() or pubsetbuf(). Note that there is no other ctor
   * such that when an object of this type is created, the internal buffer
   * is set and ready.
   * @param[in,out] buff Writable storage used by the stream buffer.
   * @param[in] max_size Capacity of @p buff in characters.
   */
  ostreambuff(CharT *buff, size_t max_size);

  /**
   * @brief Finalize the caller-owned output range.
   *
   * Before the end, make sure the size of the external vector is set to the
   * exact amount of data it contains.
   */
  ~ostreambuff();

  /// @brief Return the amount of data currently in the buffer.
  /// @return Number of written characters as size_t.
  size_t size() const;

  /**
   * Return the total capacity of the underlying buffer (size of the external
   * vector).
   * @return Maximum number of characters as size_t.
   */
  size_t capacity() const;

  /** @brief Show the buffer state for debugging.
   * @param[in,out] os Destination stream.
   * @param[in] show_content Whether to include stored characters.
   * @return Reference to @p os. */
  std::ostream &print(std::ostream &os, bool show_content = false) const;

  /**
   * @brief Reduce the writable extent to the amount of stored data.
   *
   * Limit the buffer capacity to the exact amount of data currently hold in it.
   */
  void shrink_to_fit();

private:
  char_type *const buf; ///< Caller-owned writable storage backing the stream.
  size_t m_capacity;    ///< The maximum amount of data allowed
};

/**
 * Wraps an existing vector to use it as the internal buffer of streambuf,
 * which then is used to construct an object of basic_istream (or one derived
 * from it).
 * Users must make sure that the external vector object outlives the object of
 * this type.
 */
template <typename CharT, typename Traits = std::char_traits<CharT>>
class istreambuff : public std::basic_streambuf<CharT, Traits> {
public:
  using char_type = typename std::basic_streambuf<
      CharT, Traits>::char_type; ///< Character type stored in the buffer.
  using traits_type =
      typename std::basic_streambuf<CharT,
                                    Traits>::traits_type; ///< Character-traits
                                                          ///< type.
  using int_type = typename std::basic_streambuf<
      CharT, Traits>::int_type; ///< Traits-compatible integer character type.
  using pos_type =
      typename std::basic_streambuf<CharT,
                                    Traits>::pos_type; ///< Stream-position
                                                       ///< type.
  using off_type =
      typename std::basic_streambuf<CharT,
                                    Traits>::off_type; ///< Stream-offset type.

  istreambuff() = delete; ///< Default construction is disabled.

  /** @brief Bind the stream to an existing read-only character range.
   * @param[in] data Beginning of the caller-owned character range.
   * @param[in] sz Number of readable characters. */
  istreambuff(const CharT *data, size_t sz);

  /// @brief Return the amount of data currently in the buffer.
  /// @return Number of readable characters as size_t.
  size_t size() const;

  /** @brief Show the buffer state for debugging.
   * @param[in,out] os Destination stream.
   * @param[in] show_content Whether to include stored characters.
   * @return Reference to @p os. */
  std::ostream &print(std::ostream &os, bool show_content = false) const;

private:
  const char_type *const
      buf;             ///< Caller-owned read-only storage backing the stream.
  const size_t m_size; ///< The amount of data stored
};

/**
 * Wraps an existing buffer to use it as the internal buffer of streambuf,
 * which then is used to construct an object of basic_iostream (or one derived
 * from it). Users must make sure that the external buffer allocation outlives
 * the object of this type.
 */
template <typename CharT, typename Traits = std::char_traits<CharT>>
class streambuff : public std::basic_streambuf<CharT, Traits> {
public:
  using char_type = typename std::basic_streambuf<
      CharT, Traits>::char_type; ///< Character type stored in the buffer.
  using traits_type =
      typename std::basic_streambuf<CharT,
                                    Traits>::traits_type; ///< Character-traits
                                                          ///< type.
  using int_type = typename std::basic_streambuf<
      CharT, Traits>::int_type; ///< Traits-compatible integer character type.
  using pos_type =
      typename std::basic_streambuf<CharT,
                                    Traits>::pos_type; ///< Stream-position
                                                       ///< type.
  using off_type =
      typename std::basic_streambuf<CharT,
                                    Traits>::off_type; ///< Stream-offset type.

  streambuff() = delete; ///< Default construction is disabled.

  /**
   * @brief Bind a bidirectional stream buffer to caller-owned storage.
   *
   * Set the internal buffer space with the that of the given vector, instead
   * of relying on setbuf() or pubsetbuf(). Note that there is no other ctor
   * such that when an object is created, the internal buffer is set and ready.
   * The second argument indicates whether the vector already contains data,
   * or empty.
   * @param[in,out] vec Storage used for input and output.
   * @param[in] max_size Capacity of @p vec in characters.
   * @param[in] cur_size Number of characters initially available to read.
   */
  streambuff(CharT *vec, size_t max_size, size_t cur_size = 0ul);

  /**
   * @brief Finalize the caller-owned bidirectional range.
   *
   * Before the end, make sure the size of the external vector is set to the
   * exact amount of data it contains.
   */
  ~streambuff();

  /// @brief Return the amount of data currently in the buffer.
  /// @return Number of written characters as size_t.
  size_t size() const;

  /**
   * Return the total capacity of the underlying buffer (size of the external
   * vector).
   * @return Maximum number of characters as size_t.
   */
  size_t capacity() const;

  /** @brief Show the buffer state for debugging.
   * @param[in,out] os Destination stream.
   * @param[in] show_content Whether to include stored characters.
   * @return Reference to @p os. */
  std::ostream &print(std::ostream &os, bool show_content = false) const;

  /**
   * @brief Reduce the buffer extent to the amount of stored data.
   *
   * Reduce the amount of memory used by the buffer to the exact amount needed.
   */
  void shrink_to_fit();

protected:
  /**
   * @brief Write a sequence into the fixed-capacity buffer.
   *
   * Similar to the xsputn() of the base class.
   * Only writes when enough space is left in the buffer.
   * @param[in] s Beginning of the character sequence.
   * @param[in] count Number of characters requested.
   * @return Number of characters written as `std::streamsize`.
   */
  std::streamsize xsputn(const char_type *s, std::streamsize count) override;

  // std::streamsize xsgetn( char_type* s, std::streamsize count ) override;

private:
  char_type *buf;    ///< Caller-owned read/write storage backing the stream.
  size_t m_capacity; ///< The maximum amount of data allowed
};

/**@}*/
} // namespace dr_evt

#include "streambuff_impl.hpp"
#endif // DR_EVT_UTILS_STREAMBUFF_HPP
