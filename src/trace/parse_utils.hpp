/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file parse_utils.hpp
 * @brief Trace-field parsing, CSV preprocessing, and string helpers.
 */

#ifndef DR_EVT_TRACE_PARSE_UTILS_HPP
#define DR_EVT_TRACE_PARSE_UTILS_HPP

#include <string>
#include <vector>
#include "common.hpp"
#include "trace/epoch.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/** @brief Parse an epoch timestamp. @param[out] t Parsed timestamp. @param[in] str Input text. */
void set_by(epoch_t& t, const std::string& str);
/** @brief Parse an unsigned integer. @param[out] v Parsed value. @param[in] str Input text. */
void set_by(unsigned& v, const std::string& str);
/** @brief Parse a floating-point value. @param[out] v Parsed value. @param[in] str Input text. */
void set_by(double& v, const std::string& str);
/** @brief Parse a queue name. @param[out] q Parsed queue enum. @param[in] str Input text. */
void set_by(job_queue_t& q, const std::string& str);
/** @brief Format a queue enum. @param[in] q Queue value. @return Canonical queue name. */
std::string to_string(const job_queue_t q);

/**
 * @brief Remove leading and trailing delimiter characters.
 * @param[in] str Source string.
 * @param[in] whitespace Characters considered removable whitespace.
 * @return Trimmed string value.
 */
std::string trim(const std::string& str,
                  const std::string& whitespace);

/// Remove leading and trailing whitespace.
inline std::string trim(const std::string& str)
{
    return trim(str, " \t");
}

/**
 * @brief Locate CSV field ranges in one line.
 * @param[in] str CSV input line.
 * @return Half-open [start, end) ranges for each field.
 */
std::vector<substr_pos_t> comma_separate(const std::string& str);

/**
 *  This is a data-specific helper routine.
 *  Replace the comma, which is a delimiter, within a (double) quotation.
 *  Without this, parsing comma-sepated-value data may result in an error.
 *  Does not handle a case as "'...,..."' where quotation is done erroneously.
 */
void replace_comma_within_quotation(std::string& line);


/**
 * @brief Perform a case-insensitive substring search.
 * @param[in] str String to search.
 * @param[in] sub Candidate substring.
 * @return true when sub occurs in str without regard to case.
 */
bool search_ci(const std::string& str, const std::string& sub);

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_PARSE_UTILS_HPP
