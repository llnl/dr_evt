# Setup pybind11 for Python bindings

option(DR_EVT_BUILD_PYTHON "Build Python bindings" OFF)

if(DR_EVT_BUILD_PYTHON)
    message(STATUS "Python bindings enabled")

    # Find Python
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
    message(STATUS "Python3 found: ${Python3_EXECUTABLE}")
    message(STATUS "Python3 include: ${Python3_INCLUDE_DIRS}")
    message(STATUS "Python3 libraries: ${Python3_LIBRARIES}")

    # Try to find pybind11
    find_package(pybind11 CONFIG QUIET)

    if(NOT pybind11_FOUND)
        message(STATUS "pybind11 not found, fetching from GitHub...")

        include(FetchContent)
        FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.11.1
        )
        FetchContent_MakeAvailable(pybind11)

        message(STATUS "pybind11 fetched successfully")
    else()
        message(STATUS "pybind11 found: ${pybind11_DIR}")
    endif()

    set(DR_EVT_HAS_PYBIND11 TRUE CACHE BOOL "pybind11 is available")
else()
    message(STATUS "Python bindings disabled (use -DDR_EVT_BUILD_PYTHON=ON to enable)")
    set(DR_EVT_HAS_PYBIND11 FALSE CACHE BOOL "pybind11 is not available")
endif()
