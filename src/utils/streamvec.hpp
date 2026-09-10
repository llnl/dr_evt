/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_UTILS_STREAMVEC_HPP
#define DR_EVT_UTILS_STREAMVEC_HPP

#include <streambuf>

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

/**
 * Wraps an existing vector to use it as the internal buffer of streambuf,
 * which is then used to construct an object of basic_ostream (or one derived
 * from it). The ostream will use it as its internal streambuf.
 * Users must make sure that the external vector object outlives the object of
 * this type. This streambuf will increase the size of the underlying buffer
 * space, which is the size of the external vector, as needed.
 * In addition, the space reserving method is provided such that users can
 * preallocate the necessary space in advance to avoid the reallocation
 * overhead. Users have no way to avoid such an overhead when using stringstream
 * with a binary archive in Cereal.
 */
template <typename CharT, typename Traits = std::char_traits<CharT>>
class ostreamvec : public std::basic_streambuf<CharT, Traits> {
public:
  using char_type = typename std::basic_streambuf<
      CharT, Traits>::char_type; ///< Character type stored in the vector.
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

  ostreamvec() = delete; ///< Default construction is disabled.

  /**
   * @brief Bind an output stream buffer to a caller-owned vector.
   *
   * Set the internal buffer with the buffer of the given vector, instead of
   * relying on setbuf() or pubsetbuf(). Note that there is no other ctor
   * such that when an object of this type is created, the internal buffer
   * is set and ready.
   * @param[in,out] vec Vector that receives stream output.
   */
  ostreamvec(std::vector<CharT> &vec);

  /**
   * @brief Finalize the caller-owned output vector.
   *
   * Before the end, make sure the size of the external vector is set to the
   * exact amount of data it contains.
   */
  ~ostreamvec();

  /// @brief Return the amount of data currently in the buffer.
  /// @return Number of written characters as size_t.
  size_t size() const;

  /**
   * Return the total capacity of the underlying buffer (size of the external
   * vector).
   * @return Allocated character capacity as size_t.
   */
  size_t capacity() const;

  /** @brief Show the buffer state for debugging.
   * @param[in,out] os Destination stream.
   * @param[in] show_content Whether to include stored characters.
   * @return Reference to @p os. */
  std::ostream &print(std::ostream &os, bool show_content = false) const;

  /**
   * @brief Release vector capacity beyond the stored data.
   *
   * Reduce the amount of memory used by the buffer to the exact amount needed.
   */
  void shrink_to_fit();

  /**
   * @brief Reserve storage for future output.
   *
   * Reallocate the buffer space as needed by resizing the underlying vector
   * @param[in] n Minimum requested capacity in characters.
   */
  void reserve(size_t n);

protected:
  /**
   * @brief Write a character sequence, growing the vector as needed.
   *
   * Similar to the xsputn() of the base class except for increasing the buffer
   * capacity to accomodate whole data without failure in case of overflow.
   * @param[in] s Beginning of the character sequence.
   * @param[in] count Number of characters requested.
   * @return Number of characters written as `std::streamsize`.
   */
  std::streamsize xsputn(const char_type *s, std::streamsize count) override;

  /**
   * @brief Grow the vector to accept one additional character.
   *
   * Called inside of sputc() which is not a virtual function itself. This is
   * to make sputc() increase the internal buffer size in case of overflow.
   * @param[in] c Character to store, or `traits_type::eof()`.
   * @return The stored character as int_type, or `traits_type::eof()` on
   * failure.
   */
  int_type overflow(int_type c = traits_type::eof()) override;

private:
  std::vector<char_type> &buf; ///< Caller-owned vector receiving stream output.
};

/**
 * Wraps an existing vector to use it as the internal buffer of streambuf,
 * which then is used to construct an object of basic_istream (or one derived
 * from it).
 * Users must make sure that the external vector object outlives the object of
 * this type.
 */
template <typename CharT, typename Traits = std::char_traits<CharT>>
class istreamvec : public std::basic_streambuf<CharT, Traits> {
public:
  using char_type = typename std::basic_streambuf<
      CharT, Traits>::char_type; ///< Character type stored in the vector.
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

  istreamvec() = delete; ///< Default construction is disabled.

  /** @brief Bind the stream to a caller-owned read-only vector.
   * @param[in] vec Vector supplying stream input. */
  istreamvec(const std::vector<CharT> &vec);

  /// @brief Return the amount of data currently in the buffer.
  /// @return Number of readable characters as size_t.
  size_t size() const;

  /** @brief Show the buffer state for debugging.
   * @param[in,out] os Destination stream.
   * @param[in] show_content Whether to include stored characters.
   * @return Reference to @p os. */
  std::ostream &print(std::ostream &os, bool show_content = false) const;

private:
  const std::vector<CharT>
      &buf; ///< Caller-owned vector supplying stream input.
};

/**
 * Wraps an existing vector to use it as the internal buffer of streambuf,
 * which then is used to construct an object of basic_iostream (or one derived
 * from it).
 * Users must make sure that the external vector object outlives the object of
 * this type. This streambuf will increase the size of the underlying buffer
 * as needed.
 */
template <typename CharT, typename Traits = std::char_traits<CharT>>
class streamvec : public std::basic_streambuf<CharT, Traits> {
public:
  using char_type = typename std::basic_streambuf<
      CharT, Traits>::char_type; ///< Character type stored in the vector.
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

  streamvec() = delete; ///< Default construction is disabled.

  /**
   * @brief Bind a bidirectional stream buffer to a caller-owned vector.
   *
   * Set the internal buffer space with the that of the given vector, instead
   * of relying on setbuf() or pubsetbuf(). Note that there is no other ctor
   * such that when an object is created, the internal buffer is set and ready.
   * The second argument indicates whether the vector already contains data,
   * or empty.
   * @param[in,out] vec Vector used for input and output.
   * @param[in] with_initial_data Whether existing vector elements are
   * initially available for reading.
   */
  streamvec(std::vector<CharT> &vec, bool with_initial_data = false);

  /**
   * @brief Finalize the caller-owned bidirectional vector.
   *
   * Before the end, make sure the size of the external vector is set to the
   * exact amount of data it contains.
   */
  ~streamvec();

  /// @brief Return the amount of data currently in the buffer.
  /// @return Number of written characters as size_t.
  size_t size() const;

  /**
   * Return the total capacity of the underlying buffer (size of the external
   * vector).
   * @return Allocated character capacity as size_t.
   */
  size_t capacity() const;

  /** @brief Show the buffer state for debugging.
   * @param[in,out] os Destination stream.
   * @param[in] show_content Whether to include stored characters.
   * @return Reference to @p os. */
  std::ostream &print(std::ostream &os, bool show_content = false) const;

  /**
   * @brief Release vector capacity beyond the stored data.
   *
   * Reduce the amount of memory used by the buffer to the exact amount needed.
   */
  void shrink_to_fit();

  /**
   * @brief Reserve storage for future input or output.
   *
   * Reallocate the buffer space as needed by resizing the underlying vector
   * @param[in] n Minimum requested capacity in characters.
   */
  void reserve(size_t n);

protected:
  /**
   * @brief Write a character sequence, growing the vector as needed.
   *
   * Similar to the xsputn() of the base class except for increasing the buffer
   * capacity to accomodate whole data without failure in case of overflow.
   * @param[in] s Beginning of the character sequence.
   * @param[in] count Number of characters requested.
   * @return Number of characters written as `std::streamsize`.
   */
  std::streamsize xsputn(const char_type *s, std::streamsize count) override;

  /*
   * Similar to the xsgetn() of the base class except for updating the internal
   * pointers relevent to read operation based on the current write position.
   */
  // std::streamsize xsgetn( char_type* s, std::streamsize count ) override;

  /**
   * @brief Grow the vector to accept one additional character.
   *
   * Called inside of sputc() which is not a virtual function itself. This is
   * to make sputc() increase the internal buffer size in case of overflow.
   * @param[in] c Character to store, or `traits_type::eof()`.
   * @return The stored character as int_type, or `traits_type::eof()` on
   * failure.
   */
  int_type overflow(int_type c = traits_type::eof()) override;

private:
  std::vector<char_type>
      &buf; ///< Caller-owned vector used for bidirectional I/O.
};

/**@}*/
} // namespace dr_evt

#include "streamvec_impl.hpp"
#endif // DR_EVT_UTILS_STREAMVEC_HPP
