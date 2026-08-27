# Setup for Boost using FetchContent
# Similar to Protobuf setup

cmake_minimum_required(VERSION 3.16)

# Try to find Boost first
if (DEFINED BOOST_ROOT)
    message(STATUS "BOOST_ROOT: " ${BOOST_ROOT})
    set(Boost_NO_SYSTEM_PATHS ON)
else ()
    if (DEFINED ENV{BOOST_ROOT})
        message(STATUS "ENV BOOST_ROOT: " $ENV{BOOST_ROOT})
        set(Boost_NO_SYSTEM_PATHS ON)
    endif ()
endif ()

find_package(Boost QUIET COMPONENTS regex filesystem system program_options)

if(NOT Boost_FOUND)
    # If Boost is missing, install it via FetchContent
    message(STATUS "Installing Boost via FetchContent (this may take 10-15 minutes)...")
    include(FetchContent)

    FetchContent_Declare(
        Boost
        URL https://sourceforge.net/projects/boost/files/boost/1.84.0/boost_1_84_0.tar.bz2/download
        DOWNLOAD_NAME boost_1_84_0.tar.bz2
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

    set(BOOST_INCLUDE_LIBRARIES regex filesystem system program_options)
    set(BOOST_ENABLE_CMAKE ON)

    FetchContent_MakeAvailable(Boost)

    # Set the variables that DR_EVT expects
    set(Boost_FOUND TRUE CACHE BOOL "Boost found via FetchContent")
    set(Boost_INCLUDE_DIRS "${boost_SOURCE_DIR}" CACHE PATH "Boost include directories")
    set(Boost_INCLUDE_DIR "${boost_SOURCE_DIR}" CACHE PATH "Boost include directory")

    # Set library targets - Boost CMake creates targets with Boost:: prefix
    set(Boost_LIBRARIES
        Boost::regex
        Boost::filesystem
        Boost::system
        Boost::program_options
        CACHE STRING "Boost libraries")

    # Also set them in current scope
    set(Boost_INCLUDE_DIRS "${boost_SOURCE_DIR}")
    set(Boost_INCLUDE_DIR "${boost_SOURCE_DIR}")
    set(Boost_LIBRARIES
        Boost::regex
        Boost::filesystem
        Boost::system
        Boost::program_options)

    message(STATUS "Boost installed via FetchContent at: ${boost_SOURCE_DIR}")
    message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
else()
    message(STATUS "Found Boost: ${Boost_INCLUDE_DIRS}")
    message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
endif()
