# Setup gRPC (and, transitively, Protobuf) for the DR_EVT client/server.
#
# Tries find_package() for both; if either is missing, fetches and
# builds gRPC from source (which bundles a compatible Protobuf).

find_package(Threads REQUIRED)

option(protobuf_MODULE_COMPATIBLE TRUE)

# Skips gRPC/Protobuf's default system search paths entirely - useful
# when a system-wide install exists but is broken (e.g. a Python-
# package-bundled gRPC/Protobuf whose CMake config references a protoc
# binary that doesn't exist). find_package() can still succeed via an
# explicit HINTS path even with this on.
option(AVOID_SYSTEM_GRPC "Do not search default system paths for gRPC/Protobuf" FALSE)
if (AVOID_SYSTEM_GRPC)
  set(DR_EVT_GRPC_SEARCH_MODE NO_DEFAULT_PATH)
else()
  set(DR_EVT_GRPC_SEARCH_MODE "")
endif()

# DR_EVT_GRPC_FETCHCONTENT is a CACHE variable: once this project has
# fallen back to FetchContent, find_package() is guaranteed to fail
# again on every later configure (the FetchContent-populated _deps
# directory is never a location find_package() searches), so repeating
# that search is pure wasted filesystem traversal. Caching the decision
# lets later configures skip straight to the FetchContent branch.
#
# The "found successfully" case is NOT cached the same way: targets
# (gRPC::grpc++, protobuf::libprotobuf, etc.) are not persisted across
# configures like CACHE variables are, and find_package() is the only
# thing that creates them - skipping it there would leave them
# undefined. Only the FetchContent branch is safe to skip re-running
# find_package() for, since its own add_subdirectory() recreates its
# targets fresh every configure regardless.
#
# To re-evaluate this decision (e.g. a working system gRPC becomes
# available later), clear it explicitly: `cmake -U DR_EVT_GRPC_FETCHCONTENT ..`
if (DR_EVT_GRPC_FETCHCONTENT)
  message(STATUS "Using cached decision from a previous configure: "
                  "building gRPC/Protobuf via FetchContent (skipping "
                  "the find_package() search, since it would only fail "
                  "again).")
else()
  find_package(gRPC CONFIG QUIET ${DR_EVT_GRPC_SEARCH_MODE})

  if (gRPC_FOUND)
    message(STATUS "Found gRPC: ${gRPC_VERSION} (gRPC_DIR: ${gRPC_DIR})")
    # Try Protobuf where gRPC's own config was found first (HINTS
    # augments, not replaces, the default search).
    find_package(Protobuf CONFIG QUIET HINTS ${gRPC_DIR} ${DR_EVT_GRPC_SEARCH_MODE})

    if (NOT Protobuf_FOUND)
      # Debian/Ubuntu's protobuf-compiler-grpc ships a CMake config, but
      # libprotobuf-dev doesn't - MODULE mode finds the library/headers/
      # protoc directly and creates the same targets CONFIG mode would.
      find_package(Protobuf MODULE QUIET ${DR_EVT_GRPC_SEARCH_MODE})
    endif (NOT Protobuf_FOUND)
  endif (gRPC_FOUND)

  # Deliberately a separate if-block, not an else() on the one above:
  # gRPC found but Protobuf never found must still fall back to
  # FetchContent.
  if (NOT Protobuf_FOUND OR NOT gRPC_FOUND)
    set(DR_EVT_GRPC_FETCHCONTENT ON CACHE BOOL
        "Whether gRPC/Protobuf are built via FetchContent (cached: computed once)")
  endif()
endif()

if (DR_EVT_GRPC_FETCHCONTENT)
  if (EXISTS "${CMAKE_BINARY_DIR}/_deps/grpc-src/CMakeLists.txt")
    message(STATUS  "Reusing gRPC source already fetched under "
                    "${CMAKE_BINARY_DIR}/_deps/grpc-src (not re-downloading).")
  else()
    message(STATUS  "gRPC and/or Protobuf not found as installed packages - "
                    "building gRPC (with its own bundled Protobuf) via "
                    "FetchContent instead. This will download gRPC's full "
                    "source tree, which can take a while the first time.")
  endif()

  include(FetchContent)
  set(ABSL_ENABLE_INSTALL ON)
  set(gRPC_INSTALL ON)

  # Pinned version. Bumping this should come with re-verifying
  # dr_evt_server.cpp/dr_evt_client.cpp's gRPC C++ API usage against
  # whatever that release's API surface looks like.
  #
  # CMAKE_POLICY_VERSION_MINIMUM: gRPC bundles third-party dependencies
  # (e.g. c-ares) whose CMakeLists.txt specify a cmake_minimum_required()
  # below 3.5, which recent CMake refuses outright. This applies
  # policies as if 3.5 had been requested for those subprojects.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

  # Suppress policy warnings from gRPC's third-party dependencies (RE2, etc.)
  if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
  endif()

  # gRPC's bundled Abseil emits both x86_64 and arm64 SIMD flags on
  # every Apple build via -Xarch_<arch> pairs, which a single-arch
  # compiler rejects. The PATCH_COMMAND below restricts that loop to
  # the actual target arch; this just sets CMAKE_OSX_ARCHITECTURES
  # explicitly so the patch has something to work with.
  if (APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE DR_EVT_HOST_ARCH
                     OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_OSX_ARCHITECTURES "${DR_EVT_HOST_ARCH}")
    unset(DR_EVT_HOST_ARCH)
  endif()

  # Force static for gRPC's own dependencies (upb, Abseil, etc.) -
  # shared would otherwise fail to link on macOS. Restored after.
  set(DR_EVT_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)

  # gRPC's own tests - and BoringSSL's bundled googletest they pull
  # in - are memory-hungry to compile and unused by dr_evt. Skipping
  # them avoids OOM-killing the build on memory-constrained machines
  # (e.g. `make -j$(nproc)` on standard GitHub Actions runners).
  set(DR_EVT_SAVED_BUILD_TESTING ${BUILD_TESTING})
  set(BUILD_TESTING OFF)
  set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)

  # dr_evt only compiles .proto files to C++, so skip gRPC's codegen
  # plugins for other languages. (Python client authors: use
  # `pip install grpcio-tools` instead - see CLIENT_SERVER_GUIDE.md.)
  set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF CACHE BOOL "" FORCE)

  # NOT set here: gRPC_SSL_PROVIDER=package would skip building
  # BoringSSL from source entirely (the biggest remaining compile
  # cost), using system OpenSSL instead. Left as an opt-in
  # (`-DgRPC_SSL_PROVIDER=package`, requires libssl-dev) since it's
  # unverified end-to-end and BoringSSL/OpenSSL can drift apart.

  # This project's own -Wall -Wextra (SetupCXX.cmake) would otherwise
  # apply to gRPC's own source too. Trailing -w overrides them
  # (compilers process flags left-to-right); restored right after.
  set(DR_EVT_SAVED_CXX_FLAGS ${CMAKE_CXX_FLAGS})
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
  if (APPLE)
    set(DR_EVT_GRPC_PATCH_COMMAND
        ${CMAKE_COMMAND} -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
                         -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}
                         -P ${CMAKE_SOURCE_DIR}/cmake/patches/fix_abseil_randen_copts.cmake
        COMMAND ${CMAKE_COMMAND}
                -P ${CMAKE_SOURCE_DIR}/cmake/patches/fix_zlib_fdopen.cmake)
  else()
    set(DR_EVT_GRPC_PATCH_COMMAND "")
  endif()

  # Without this, FetchContent re-contacts the git remote on every
  # configure to check for upstream changes, even once fully
  # downloaded. The tag is pinned, so there's nothing to check for.
  set(FETCHCONTENT_UPDATES_DISCONNECTED_GRPC ON)

  FetchContent_Declare(
    grpc
    GIT_REPOSITORY https://github.com/grpc/grpc.git
    GIT_TAG        v1.83.1
    PATCH_COMMAND  ${DR_EVT_GRPC_PATCH_COMMAND})
  FetchContent_MakeAvailable(grpc)
  unset(DR_EVT_GRPC_PATCH_COMMAND)
  set(CMAKE_CXX_FLAGS ${DR_EVT_SAVED_CXX_FLAGS})
  unset(DR_EVT_SAVED_CXX_FLAGS)
  set(BUILD_SHARED_LIBS ${DR_EVT_SAVED_BUILD_SHARED_LIBS})
  unset(DR_EVT_SAVED_BUILD_SHARED_LIBS)
  set(BUILD_TESTING ${DR_EVT_SAVED_BUILD_TESTING})
  unset(DR_EVT_SAVED_BUILD_TESTING)

  # FetchContent's add_subdirectory gives gRPC's own (non-namespaced)
  # target names directly. Alias them to the same protobuf::* / gRPC::*
  # names find_package() would produce, so the rest of this project's
  # CMake doesn't need to know which discovery path was taken.
  if (NOT TARGET protobuf::libprotobuf)
    add_library(protobuf::libprotobuf ALIAS libprotobuf)
  endif()
  if (NOT TARGET protobuf::protoc)
    add_executable(protobuf::protoc ALIAS protoc)
  endif()
  if (NOT TARGET gRPC::grpc++)
    add_library(gRPC::grpc++ ALIAS grpc++)
  endif()
  if (NOT TARGET gRPC::grpc_cpp_plugin)
    add_executable(gRPC::grpc_cpp_plugin ALIAS grpc_cpp_plugin)
  endif()
else()
  if (NOT TARGET gRPC::grpc_cpp_plugin)
    message(FATAL_ERROR
      "gRPC found, but the gRPC::grpc_cpp_plugin target is missing - "
      "check that your gRPC installation includes the C++ plugin "
      "(on Debian/Ubuntu, this is part of protobuf-compiler-grpc).")
  endif()
endif()

set(DR_EVT_HAS_GRPC TRUE)

# This module provides Protobuf too, whenever gRPC is enabled - see
# CMakeLists.txt's root-level ordering, which skips SetupProtobuf.cmake
# in that case.
set(DR_EVT_HAS_PROTOBUF TRUE)
