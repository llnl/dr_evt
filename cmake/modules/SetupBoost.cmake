# Setup for Boost using modern CMake 3.24+ standards
# Uses find_package with REQUIRED COMPONENTS and imported targets

cmake_minimum_required(VERSION 3.24)

# CMake 3.30+ deprecated FindBoost.cmake in favor of BoostConfig.cmake
# Set policy to use modern Boost CMake support
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# Skip system paths for Boost (useful when system install is broken/incompatible)
option(AVOID_SYSTEM_BOOST "Do not search default system paths for Boost" FALSE)

# Accept both CMake's package-root spelling and the longstanding uppercase
# project spelling from the environment.  A value supplied with -DBoost_ROOT
# or -DBOOST_ROOT always takes precedence.
if (NOT DEFINED Boost_ROOT AND DEFINED ENV{Boost_ROOT})
    set(Boost_ROOT "$ENV{Boost_ROOT}")
endif()
if (NOT DEFINED BOOST_ROOT AND DEFINED ENV{BOOST_ROOT})
    set(BOOST_ROOT "$ENV{BOOST_ROOT}")
endif()

# Configure search path for Boost
if (AVOID_SYSTEM_BOOST)
    message(STATUS "AVOID_SYSTEM_BOOST=ON: skipping system Boost search")
    set(Boost_NO_SYSTEM_PATHS ON)
    set(DR_EVT_BOOST_SEARCH_MODE NO_DEFAULT_PATH)
elseif (DEFINED Boost_ROOT)
    message(STATUS "Boost_ROOT: ${Boost_ROOT}")
    set(Boost_NO_SYSTEM_PATHS ON)
    set(DR_EVT_BOOST_SEARCH_MODE NO_DEFAULT_PATH)
elseif (DEFINED BOOST_ROOT)
    message(STATUS "BOOST_ROOT: ${BOOST_ROOT}")
    set(Boost_NO_SYSTEM_PATHS ON)
    set(DR_EVT_BOOST_SEARCH_MODE NO_DEFAULT_PATH)
else()
    set(DR_EVT_BOOST_SEARCH_MODE "")
endif()

# Modern CMake 3.24+ approach: find_package with REQUIRED COMPONENTS
# Automatically creates Boost::component imported targets
#
# Note: Unlike gRPC/Protobuf (which use CONFIG mode with HINTS), FindBoost
# uses MODULE mode and reads BOOST_ROOT and Boost_NO_SYSTEM_PATHS variables
# directly, plus NO_DEFAULT_PATH when AVOID_SYSTEM_BOOST is set.
# Discard discovery results from an earlier FetchContent configure before
# searching again.  This permits a subsequently installed Boost to replace
# the old _deps copy without requiring a new build directory.  The _deps
# directory is used only to invalidate stale discovery data, never as the
# decision to install Boost.
unset(DR_EVT_BOOST_FETCHCONTENT CACHE)
if (EXISTS "${CMAKE_BINARY_DIR}/_deps/boost-build")
    unset(Boost_FOUND CACHE)
    unset(Boost_FOUND)
    unset(Boost_INCLUDE_DIR CACHE)
    unset(Boost_INCLUDE_DIR)
    unset(Boost_INCLUDE_DIRS CACHE)
    unset(Boost_INCLUDE_DIRS)
    unset(Boost_LIBRARY_DIRS CACHE)
    unset(Boost_LIBRARY_DIRS)
    unset(Boost_LIBRARIES CACHE)
    unset(Boost_LIBRARIES)
    foreach(DR_EVT_BOOST_COMPONENT regex filesystem system program_options serialization container)
        string(TOUPPER "${DR_EVT_BOOST_COMPONENT}" DR_EVT_BOOST_COMPONENT_UPPER)
        unset(Boost_${DR_EVT_BOOST_COMPONENT_UPPER}_FOUND CACHE)
        unset(Boost_${DR_EVT_BOOST_COMPONENT_UPPER}_FOUND)
        unset(Boost_${DR_EVT_BOOST_COMPONENT_UPPER}_LIBRARY_RELEASE CACHE)
        unset(Boost_${DR_EVT_BOOST_COMPONENT_UPPER}_LIBRARY_RELEASE)
        unset(Boost_${DR_EVT_BOOST_COMPONENT_UPPER}_LIBRARY_DEBUG CACHE)
        unset(Boost_${DR_EVT_BOOST_COMPONENT_UPPER}_LIBRARY_DEBUG)
    endforeach()
    unset(DR_EVT_BOOST_COMPONENT_UPPER)
endif()
# Prefer a Boost package installed by a previous `cmake --install` into this
# project's nonempty prefix. HINTS remains usable with NO_DEFAULT_PATH, so
# this also works when AVOID_SYSTEM_BOOST is enabled; FetchContent remains the
# fallback. Do not add an empty prefix, which could otherwise undermine an
# explicit no-system-search configuration.
set(DR_EVT_BOOST_INSTALL_HINTS)
if (CMAKE_INSTALL_PREFIX)
    list(APPEND DR_EVT_BOOST_INSTALL_HINTS "${CMAKE_INSTALL_PREFIX}")
endif()
if (DEFINED Boost_ROOT)
    list(APPEND DR_EVT_BOOST_INSTALL_HINTS "${Boost_ROOT}")
endif()
if (DEFINED BOOST_ROOT)
    list(APPEND DR_EVT_BOOST_INSTALL_HINTS "${BOOST_ROOT}")
endif()
set(DR_EVT_BOOST_COMPONENTS
    regex
    filesystem
    system
    program_options
    serialization
    container)

# Prefer a modern exported Boost package when one is available.
find_package(Boost CONFIG QUIET COMPONENTS ${DR_EVT_BOOST_COMPONENTS}
    HINTS ${DR_EVT_BOOST_INSTALL_HINTS}
    ${DR_EVT_BOOST_SEARCH_MODE})

# The Boost CMake distribution used by FetchContent does not always install
# BoostConfig.cmake. Fall back to FindBoost for a prefix containing the normal
# include/boost and lib or lib64 layout. This is deliberately a second choice:
# CMP0167 NEW prefers an exported package when one exists.
if (NOT Boost_FOUND)
    cmake_policy(PUSH)
    if (POLICY CMP0167)
        cmake_policy(SET CMP0167 OLD)
    endif()
    if (CMAKE_INSTALL_PREFIX AND NOT DEFINED Boost_ROOT AND NOT DEFINED BOOST_ROOT)
        set(Boost_ROOT "${CMAKE_INSTALL_PREFIX}")
    endif()
    if (CMAKE_INSTALL_PREFIX AND NOT DEFINED BOOST_LIBRARYDIR)
        set(BOOST_LIBRARYDIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
    endif()
    find_package(Boost MODULE QUIET COMPONENTS ${DR_EVT_BOOST_COMPONENTS}
    )
    cmake_policy(POP)
endif()
unset(DR_EVT_BOOST_COMPONENTS)
unset(DR_EVT_BOOST_INSTALL_HINTS)

if(NOT Boost_FOUND)
    # If Boost is missing, install it via FetchContent
    message(STATUS "Installing Boost via FetchContent (this may take 10-15 minutes)...")
    include(FetchContent)

    FetchContent_Declare(
        Boost
        URL https://github.com/boostorg/boost/releases/download/boost-1.85.0/boost-1.85.0-cmake.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM  # CMake 3.25+ marks it as SYSTEM to suppress warnings
    )

    set(BOOST_INCLUDE_LIBRARIES regex filesystem system program_options serialization container multi_index circular_buffer)
    set(BOOST_ENABLE_CMAKE ON)

    # Suppress compiler warnings from third-party Boost code
    set(_dr_evt_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")

    FetchContent_MakeAvailable(Boost)

    set(CMAKE_CXX_FLAGS "${_dr_evt_saved_cxx_flags}")
    unset(_dr_evt_saved_cxx_flags)

    # Modern CMake 3.24+: Boost imported targets (Boost::component) are automatically created
    # Set additional variables for compatibility
    set(Boost_FOUND TRUE CACHE BOOL "Boost found via FetchContent")
    set(Boost_INCLUDE_DIRS
        "${boost_SOURCE_DIR}"
        "${boost_SOURCE_DIR}/libs/multi_index/include"
        "${boost_SOURCE_DIR}/libs/serialization/include"
        "${boost_SOURCE_DIR}/libs/container/include"
        "${boost_SOURCE_DIR}/libs/circular_buffer/include")
    set(Boost_INCLUDE_DIR "${boost_SOURCE_DIR}")
    set(Boost_INCLUDE_DIRS "${Boost_INCLUDE_DIRS}"
        CACHE STRING "Boost include directories" FORCE)
    set(Boost_INCLUDE_DIR "${Boost_INCLUDE_DIR}"
        CACHE PATH "Boost include directory" FORCE)

    # Boost CMake automatically creates targets with Boost:: prefix
    set(Boost_LIBRARIES
        Boost::regex
        Boost::filesystem
        Boost::system
        Boost::program_options)
    set(Boost_LIBRARIES "${Boost_LIBRARIES}"
        CACHE STRING "Boost libraries" FORCE)
    set(DR_EVT_BOOST_FETCHCONTENT ON)

    message(STATUS "Boost installed via FetchContent at: ${boost_SOURCE_DIR}")
    message(STATUS "Boost imported targets available: ${Boost_LIBRARIES}")
else()
    set(DR_EVT_BOOST_FETCHCONTENT OFF)
    # System Boost found - modern CMake 3.24+ automatically creates Boost::component targets
    message(STATUS "Found Boost: ${Boost_VERSION}")
    message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")

    # Set Boost_LIBRARIES for compatibility
    set(Boost_LIBRARIES
        Boost::regex
        Boost::filesystem
        Boost::system
        Boost::program_options)

    message(STATUS "Boost imported targets: ${Boost_LIBRARIES}")
endif()
