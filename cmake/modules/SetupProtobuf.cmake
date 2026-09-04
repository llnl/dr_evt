set(PROTOBUF_MIN_VERSION "3.0.0")

# For cross-compilation, we need to use protoc executable compiled to
# run on host (build) machines rather than on targer machines.
# This is because protoc is called while compiling the application
# code on hosts. On the other hand, we need to use the protobuf library
# compiled to run on target machines because we link it to application
# executables to run on target machines.
# In this case, users must explicitly set the path to host protoc,
# Protobuf_PROTOC_EXECUTABLE=install-path-for-host/bin/protoc
# and the path to the library separately
# PROTOBUF_DIR=install-path-for-target-lib
#
# When there is no cross-compiling need, users can provide a hint on
# whre protobuf is installed by setting the variable PROTOBUF_ROOT
# If neither of PROTOBUF_ROOT and PROTOBUF_DIR is given, protobuf
# library is build out of the source, which is downloaded from
# the online repository.

if (Protobuf_PROTOC_EXECUTABLE)
  if (PROTOBUF_DIR)
    list(APPEND CMAKE_PREFIX_PATH ${PROTOBUF_DIR})
    list(APPEND CMAKE_LIBRARY_PATH ${PROTOBUF_DIR}/lib)
    list(APPEND CMAKE_INCLUDE_PATH ${PROTOBUF_DIR}/include)

    find_package(Protobuf "${PROTOBUF_MIN_VERSION}" MODULE)

    list(REMOVE_ITEM CMAKE_PREFIX_PATH ${PROTOBUF_DIR})
    list(REMOVE_ITEM CMAKE_LIBRARY_PATH ${PROTOBUF_DIR}/lib)
    list(REMOVE_ITEM CMAKE_INCLUDE_PATH ${PROTOBUF_DIR}/include)

    if (NOT Protobuf_FOUND)
      message(FATAL_ERROR "Protobuf not found.")
    endif (NOT Protobuf_FOUND)
  else (PROTOBUF_DIR)
    message(FATAL_ERROR "Specify the target protobuf library "
                        "installation path, PROTOBUF_DIR")
  endif (PROTOBUF_DIR)
else (Protobuf_PROTOC_EXECUTABLE)
  # Skip system paths for Protobuf (useful when system install is broken/incompatible)
  option(AVOID_SYSTEM_PROTOBUF "Do not search default system paths for Protobuf" FALSE)

  if (PROTOBUF_ROOT)
    option(protobuf_MODULE_COMPATIBLE
      "Be compatible with FindProtobuf.cmake" ON)
    option(protobuf_VERBOSE
      "Enable verbose protobuf output" OFF)

    find_package(Protobuf "${PROTOBUF_MIN_VERSION}" CONFIG QUIET
      NAMES protobuf PROTOBUF
      HINTS
      "${Protobuf_ROOT}" "${PROTOBUF_ROOT}"
      "$ENV{Protobuf_ROOT}" "$ENV{PROTOBUF_ROOT}"
      PATH_SUFFIXES lib64/cmake/protobuf lib/cmake/protobuf
      NO_DEFAULT_PATH)

    if (NOT Protobuf_FOUND)
      # Redo searching without hint (unless AVOID_SYSTEM_PROTOBUF is ON)
      if (NOT AVOID_SYSTEM_PROTOBUF)
        find_package(Protobuf "${PROTOBUF_MIN_VERSION}" CONFIG QUIET REQUIRED)
      endif()
    endif (NOT Protobuf_FOUND)
  else (PROTOBUF_ROOT)
    # Search for Protobuf in FetchContent cache first (build/_deps/protobuf-src),
    # then CMAKE_INSTALL_PREFIX, then system paths
    set(_PROTOBUF_SEARCH_PATHS
      "${CMAKE_BINARY_DIR}/_deps/protobuf-build"
      "${CMAKE_BINARY_DIR}/_deps/protobuf-src/cmake"
      "${CMAKE_INSTALL_PREFIX}"
    )

    if (AVOID_SYSTEM_PROTOBUF)
      message(STATUS "AVOID_SYSTEM_PROTOBUF=ON: skipping system Protobuf search")
      # Only search in FetchContent cache, no system paths
      find_package(Protobuf "${PROTOBUF_MIN_VERSION}" CONFIG QUIET
        NAMES protobuf PROTOBUF
        HINTS ${_PROTOBUF_SEARCH_PATHS}
        PATH_SUFFIXES lib64/cmake/protobuf lib/cmake/protobuf lib/cmake
        NO_DEFAULT_PATH)
    else()
      # Search FetchContent cache first, then system paths
      find_package(Protobuf "${PROTOBUF_MIN_VERSION}" CONFIG QUIET
        NAMES protobuf PROTOBUF
        HINTS ${_PROTOBUF_SEARCH_PATHS}
        "$ENV{Protobuf_DIR}" "$ENV{PROTOBUF_DIR}"
        PATH_SUFFIXES lib64/cmake/protobuf lib/cmake/protobuf lib/cmake)
    endif()

    if (NOT Protobuf_FOUND)
      message(STATUS "Protobuf not found. Building via FetchContent.")
      set(BUILD_PROTOBUF ON)
      include(${CMAKE_SOURCE_DIR}/external/protobuf/CMakeLists.txt)
      return()
    endif (NOT Protobuf_FOUND)
  endif (PROTOBUF_ROOT)
endif (Protobuf_PROTOC_EXECUTABLE)


if (NOT Protobuf_FOUND)
  message(FATAL_ERROR "Protobuf not found.")
endif (NOT Protobuf_FOUND)

get_target_property(Protobuf_LIBRARY protobuf::libprotobuf LOCATION_RELEASE)
get_target_property(Protobuf_EXECUTABLE protobuf::protoc LOCATION_RELEASE)

message(STATUS "Found Protobuf: ${Protobuf_DIR}")
message(STATUS "Found libprotobuf: ${Protobuf_LIBRARY}")
message(STATUS "Found protoc: ${Protobuf_EXECUTABLE}")

set(DR_EVT_HAS_PROTOBUF TRUE)
