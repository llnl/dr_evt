/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_UTILS_FILE_HPP
#define DR_EVT_UTILS_FILE_HPP
#include <string>
#include <sys/stat.h> // mode_t

#if defined(__linux__)
#include <linux/limits.h> // PATH_MAX
#elif defined(__APPLE__)
#include <sys/syslimits.h> // PATH_MAX on macOS
#endif

#if !defined(PATH_MAX)
/** @brief Fallback maximum path length when the platform defines none. */
#define PATH_MAX 4096
#endif

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#if defined(DR_EVT_HAS_STD_FILESYSTEM)
#include <filesystem>
#else
#include <boost/filesystem.hpp>
#endif

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

/**
 * @brief Split a path into its parent directory, stem, and extension.
 * @param[in] path Path to split.
 * @param[out] parent_dir Parent-directory component.
 * @param[out] stem Filename component without its final extension.
 * @param[out] extension Final filename extension.
 */
void extract_file_component(const std::string path, std::string &parent_dir,
                            std::string &stem, std::string &extension);

/** @brief Append text to the stem of a path.
 * @param[in] path Original path.
 * @param[in] str Text to append before the extension.
 * @return New path containing the modified stem. */
std::string append_to_stem(const std::string path, const std::string str);

/** @brief Test whether a filesystem entry exists.
 * @param[in] filename Path to inspect.
 * @return `true` if the path exists; otherwise `false`. */
bool check_if_file_exists(const std::string filename);

/** @brief Derive the shared-library filename associated with a model file.
 * @param[in] model_filename Model path used to derive the library name.
 * @return Derived shared-library path. */
std::string get_libname_from_model(const std::string &model_filename);

/** @brief Derive the default output filename from an input filename.
 * @param[in] infilename Input path.
 * @return Default output path. */
std::string get_default_ofname_from_ifname(const std::string &infilename);

/** @brief Express a full path relative to a base path.
 * @param[in] basepath Base directory.
 * @param[in] fullpath Path to make relative.
 * @return Relative subpath, or @p fullpath when it is outside @p basepath. */
std::string get_subpath(const std::string &basepath,
                        const std::string &fullpath);

/** @brief Create a directory and any missing parent directories.
 * @param[in] path Directory path to create.
 * @param[in] m Permission mode used for newly created directories.
 * @return Zero on success, or a nonzero error status. */
int mkdir_as_needed(const std::string &path, const mode_t m = 0700);

/** @brief Synchronize a directory's metadata with stable storage.
 * @param[in] path Directory to synchronize.
 * @return `true` on success; otherwise `false`. */
bool sync_directory(const std::string &path);

/** @brief Flush and synchronize a file-backed output stream.
 * @param[in,out] os Output stream to flush and synchronize. */
void fsync_ofstream(std::ofstream &os);

/**@}*/
} // end of namespace dr_evt
#endif //  DR_EVT_UTILS_FILE_HPP
