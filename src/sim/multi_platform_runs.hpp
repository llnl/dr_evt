/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_MULTI_PLATFORM_RUNS_HPP
#define DR_EVT_SIM_MULTI_PLATFORM_RUNS_HPP
#include <array>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <stdexcept>
#include <iomanip>
#include "common.hpp"
#include "trace/parse_utils.hpp" // trim

namespace dr_evt {
/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * @brief Store benchmark execution-time samples for N platforms.
 * @details Reads a CSV whose first column is a sample label and whose next N
 * columns are execution times. Platform names are read from the header.
 * @tparam N Number of platform-time columns expected in the input.
 */
template <size_t N>
class Multi_Platform_Runs {
  public:
    /// Execution-time value type.
    using texec_t = double;
    /// One execution-time sample across every platform.
    using sample_t = typename std::array<texec_t, N>;
    /// All execution-time samples.
    using data_t = typename std::vector<sample_t>;
    /// Names of the N platform columns.
    using header_t = typename std::array<std::string, N>;

  protected:
    /// Loaded execution-time samples.
    data_t m_data;
    /// Platform names parsed from the CSV header.
    header_t m_header;

  public:
    /** @brief Reserve storage for execution-time samples.
     * @param[in] n Requested minimum sample capacity. */
    void reserve (const size_t n);
    /** @brief Load platform execution times from CSV input.
     * @param[in] ifname Input filename.
     * @return true if the file was opened and parsed; false for an empty or unreadable filename.
     * @throws std::out_of_range if a row has an unexpected column count. */
    bool load (const std::string& ifname);
    /** @brief Return loaded execution-time samples.
     * @return Const reference to the data_t storage. */
    const data_t& get_data () const;
    /** @brief Write the loaded data as CSV.
     * @param[in,out] os Destination stream.
     * @return The same destination stream after writing. */
    std::ostream& print (std::ostream& os) const;
};

template <size_t N>
void Multi_Platform_Runs<N>::reserve (const size_t n)
{
    m_data.reserve (n);
}

template <size_t N>
bool Multi_Platform_Runs<N>::load (const std::string& ifname)
{
    if (ifname.empty ()) {
        return false;
    }

    std::ifstream ifs (ifname);
    if (!ifs) {
        return false;
    }

    size_t cnt = 0ul;

    std::string line;
    std::getline (ifs, line); // Consume the header line
    {
        cnt ++;
        std::istringstream iss (line);
        std::string str;
        std::vector<std::string> hdr_strs;

        while (std::getline(iss, str, ',')) {
            hdr_strs.emplace_back (str);
        }

        if (hdr_strs.size () != N + 1) {
            std::string err = "Number of columns with line "
                            + std::to_string (cnt) + ": "
                            + std::to_string (hdr_strs.size ())
                            + " > " + std::to_string (N);
            throw std::out_of_range (err);
        }

        for (size_t i = 0ul; i < N; ++i) {
            m_header[i] = trim (hdr_strs[i+1]);
        }
    }

    while (std::getline (ifs, line)) { // Read a line
        cnt ++;
        std::istringstream iss (line);
        std::string str;
        std::vector<std::string> val_strs;

        while (std::getline(iss, str, ',')) {
            val_strs.emplace_back (str);
            str.clear ();
        }

        if (val_strs.size () != N + 1) {
            std::string err = "Number of columns with line "
                            + std::to_string (cnt) + ": "
                            + std::to_string (val_strs.size ())
                            + " > " + std::to_string (N);
            throw std::out_of_range (err);
        }

        m_data.push_back ({});
        auto& sample = m_data.back ();
        for (size_t i = 0ul; i < N; ++i) {
            sample[i] = static_cast<texec_t> (std::stod(val_strs[i+1]));
        }
        line.clear ();
    }

    ifs.close ();

    return true;
}

template <size_t N>
const typename Multi_Platform_Runs<N>::data_t& Multi_Platform_Runs<N>::get_data () const
{
    return m_data;
}

template <size_t N>
std::ostream& Multi_Platform_Runs<N>::print (std::ostream& os) const
{
    using std::to_string;
    using std::string;

    string line;
    for (size_t i = 0ul; i < N; ++i) {
        line += ',' + m_header [i];
    }
    os << line << std::endl;

    size_t cnt = 0ul;

    // Relying on ostringstream instead of string for controlling precision
    std::ostringstream oss;

    oss << std::setprecision (9);
    for (const auto& sample: m_data) {
        oss << cnt ++;
        //line = to_string (cnt++);
        for (size_t i = 0ul; i < N; ++i) {
            oss << ',' << sample[i];
            //line += ',' + to_string (sample[i]);
        }
        os << oss.str () << std::endl;
        oss.str(std::string());
        //os << line << std::endl;
    }

    return os;
}

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_MULTI_PLATFORM_RUNS_HPP
