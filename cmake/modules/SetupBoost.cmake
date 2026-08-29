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

# Check if all required component libraries were actually found
set(BOOST_LIBS_FOUND TRUE)
if(Boost_FOUND)
    foreach(component REGEX FILESYSTEM SYSTEM PROGRAM_OPTIONS)
        if(NOT Boost_${component}_LIBRARY_RELEASE AND NOT Boost_${component}_LIBRARY)
            set(BOOST_LIBS_FOUND FALSE)
            message(STATUS "Boost component ${component} library not found")
        endif()
    endforeach()
endif()

if(NOT Boost_FOUND OR NOT BOOST_LIBS_FOUND)
    # If Boost is missing, install it via FetchContent
    message(STATUS "Installing Boost via FetchContent (this may take 10-15 minutes)...")
    include(FetchContent)

    FetchContent_Declare(
        Boost
        URL https://github.com/boostorg/boost/releases/download/boost-1.85.0/boost-1.85.0-cmake.tar.xz
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

    # If find_package used the old FindBoost.cmake module (not BoostConfig.cmake),
    # it may not have created IMPORTED targets. Check and create them if needed.
    if(NOT TARGET Boost::regex)
        message(STATUS "Creating Boost IMPORTED targets (FindBoost didn't create them)")

        foreach(component regex filesystem system program_options)
            string(TOUPPER ${component} component_upper)
            if(Boost_${component_upper}_LIBRARY)
                add_library(Boost::${component} UNKNOWN IMPORTED)
                set_target_properties(Boost::${component} PROPERTIES
                    IMPORTED_LOCATION "${Boost_${component_upper}_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIRS}")
                message(STATUS "  Created Boost::${component} -> ${Boost_${component_upper}_LIBRARY}")
            endif()
        endforeach()
    endif()
endif()
