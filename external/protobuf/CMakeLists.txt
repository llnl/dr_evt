# Setup Protobuf using FetchContent (consistent with Boost, gRPC, pybind11)
# Replaces old ExternalProject approach

if (BUILD_PROTOBUF)
  message(STATUS "Building Protobuf via FetchContent...")

  include(FetchContent)

  # Pinned version - v21.12 (compatible with modern CMake)
  set(PROTOBUF_TAG "v21.12" CACHE STRING "The git tag to checkout for Protobuf")

  # Suppress compiler warnings from third-party Protobuf code
  set(_dr_evt_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")

  # Disable Protobuf tests and examples
  set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(protobuf_MODULE_COMPATIBLE ON CACHE BOOL "" FORCE)

  # Without this, FetchContent re-contacts git on every configure
  set(FETCHCONTENT_UPDATES_DISCONNECTED_protobuf ON)

  FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG        ${PROTOBUF_TAG}
  )

  FetchContent_MakeAvailable(protobuf)

  # Restore flags
  set(CMAKE_CXX_FLAGS "${_dr_evt_saved_cxx_flags}")
  unset(_dr_evt_saved_cxx_flags)

  # Create aliases if needed (FetchContent gives non-namespaced targets)
  if (NOT TARGET protobuf::libprotobuf)
    add_library(protobuf::libprotobuf ALIAS libprotobuf)
  endif()
  if (NOT TARGET protobuf::protoc)
    add_executable(protobuf::protoc ALIAS protoc)
  endif()

  # Set variable for SetupProtobuf.cmake to find
  set(PROTOBUF_DIR "${protobuf_BINARY_DIR}" CACHE INTERNAL "Protobuf build directory")
  set(Protobuf_FOUND TRUE CACHE BOOL "Protobuf found via FetchContent")

  message(STATUS "Protobuf fetched to: ${protobuf_SOURCE_DIR}")
endif()
