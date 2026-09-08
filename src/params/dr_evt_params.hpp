/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file dr_evt_params.hpp
 * @brief Top-level command-line options for selecting DR_EVT setup files.
 */

#ifndef DR_EVT_PARAMS_DR_EVT_PARAMS_HPP
#define DR_EVT_PARAMS_DR_EVT_PARAMS_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
//#error "no config"
#endif

namespace dr_evt {
/** \addtogroup dr_evt_params
 *  @{ */

/** @brief Top-level configuration-file options shared by DR_EVT tools. */
struct cmd_line_opts {
    /// Combined simulation-and-trace protobuf setup filename.
    std::string m_all_setup;
    /// Simulation-only protobuf setup filename.
    std::string m_sim_setup;
    /// Trace-tool-only protobuf setup filename.
    std::string m_trace_setup;
    /// true once at least one setup option was supplied.
    bool m_is_set;

    /** @brief Parse top-level configuration options.
     * @param[in] argc Argument count.
     * @param[in] argv Argument vector.
     * @return true when parsing succeeds. */
    bool parse_cmd_line(int argc, char** argv);
    /** @brief Write selected setup files to standard output. */
    void show() const;
};

/**@}*/
} // end of namespace dr_evt

#endif // DR_EVT_PARAMS_DR_EVT_PARAMS_HPP
