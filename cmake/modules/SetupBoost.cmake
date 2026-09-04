# Setup for Boost using modern CMake 3.24+ standards
# Uses find_package with REQUIRED COMPONENTS and imported targets

cmake_minimum_required(VERSION 3.24)

# CMake 3.30+ deprecated FindBoost.cmake in favor of BoostConfig.cmake
# Set policy to use modern Boost CMake support
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# Configure search path for Boost
if (DEFINED BOOST_ROOT)
    message(STATUS "BOOST_ROOT: ${BOOST_ROOT}")
    set(Boost_NO_SYSTEM_PATHS ON)
elseif (DEFINED ENV{BOOST_ROOT})
    message(STATUS "ENV BOOST_ROOT: $ENV{BOOST_ROOT}")
    set(Boost_NO_SYSTEM_PATHS ON)
endif ()

# Modern CMake 3.24+ approach: find_package with REQUIRED COMPONENTS
# Automatically creates Boost::component imported targets
find_package(Boost QUIET COMPONENTS
    regex
    filesystem
    system
    program_options
    serialization
    container
)

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
        "${boost_SOURCE_DIR}/libs/circular_buffer/include"
        CACHE PATH "Boost include directories")
    set(Boost_INCLUDE_DIR "${boost_SOURCE_DIR}" CACHE PATH "Boost include directory")

    # Boost CMake automatically creates targets with Boost:: prefix
    set(Boost_LIBRARIES
        Boost::regex
        Boost::filesystem
        Boost::system
        Boost::program_options
        CACHE STRING "Boost libraries")

    message(STATUS "Boost installed via FetchContent at: ${boost_SOURCE_DIR}")
    message(STATUS "Boost imported targets available: ${Boost_LIBRARIES}")
else()
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
