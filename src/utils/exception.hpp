/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_UTILS_EXCEPTION_HPP
#define DR_EVT_UTILS_EXCEPTION_HPP
#include <exception>
#include <iostream>
#include <string>

#if 0 // Intel compiler 19.1.2 fails to compile this
#define DR_EVT_THROW(_MSG_)                                                    \
  do {                                                                         \
    throw dr_evt::exception(std::string(__FILE__) + " : line " +               \
                            std::to_string(__LINE__) + " : " + _MSG_ + '\n');  \
  } while (0)
#else
/** @brief Throw a dr_evt::exception annotated with source file and line.
 * @details The macro argument is a string expression containing the diagnostic
 * message; Doxygen does not reliably expose underscored macro arguments as
 * parameters. */
#define DR_EVT_THROW(_MSG_)                                                    \
  throw dr_evt::exception(std::string(__FILE__) + " : line " +                 \
                          std::to_string(__LINE__) + " : " + _MSG_ + '\n')
#endif

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

/** @brief Scheduler-specific exception carrying an explanatory message. */
class exception : public std::exception {
public:
  /** @brief Construct an exception with an optional message.
   * @param[in] message Diagnostic text retained by the exception. */
  exception(const std::string message = "");
  /** @brief Return the exception message as a C string.
   * @return Pointer to null-terminated storage owned by this exception. */
  const char *what() const noexcept override;

private:
  /** @brief Message retained for `what()` and stream output. */
  std::string m_message;
};

/** @brief Write an exception's message to an output stream.
 * @param[in,out] os Destination stream.
 * @param[in] e Exception whose message is written.
 * @return Reference to @p os. */
std::ostream &operator<<(std::ostream &os, const exception &e);

/**@}*/
} // end of namespace dr_evt
#endif //  DR_EVT_UTILS_EXCEPTION_HPP
