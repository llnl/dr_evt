# Setup gRPC (and, transitively, Protobuf) for the DR_EVT client/server.
#
# Mirrors the standard gRPC-recommended CMake pattern (see
# https://github.com/grpc/grpc/blob/master/examples/cpp/cmake/common.cmake):
# silently check whether gRPC and Protobuf are already installed; if not,
# fetch and build gRPC from source (which bundles a compatible Protobuf).

find_package(Threads REQUIRED)

option(protobuf_MODULE_COMPATIBLE TRUE)

# Lets a caller opt out of searching for gRPC/Protobuf in the system's
# default search paths entirely (CMAKE_PREFIX_PATH, CMAKE_SYSTEM_PREFIX_PATH,
# etc.) - useful when a system-wide install exists but is broken or
# otherwise undesired (e.g. a Python-package-bundled gRPC/Protobuf whose
# CMake config references a protoc binary that doesn't actually exist).
# With this ON, find_package() below can still succeed via an explicit
# HINTS path, but never via the default search locations.
option(AVOID_ENV_GRPC "Do not search default system paths for gRPC/Protobuf" FALSE)
if (AVOID_ENV_GRPC)
  set(DR_EVT_GRPC_SEARCH_MODE NO_DEFAULT_PATH)
else()
  set(DR_EVT_GRPC_SEARCH_MODE "")
endif()

# gRPC first, then Protobuf - and only search Protobuf independently if
# gRPC was actually found. find_package() is idempotent, so whichever
# Protobuf is found first gets "locked in" for the rest of this configure.
# Searching gRPC first lets its own CMake config pull in the Protobuf it
# was actually built against; skipping the Protobuf search entirely when
# gRPC isn't found avoids handing FetchContent's own, bundled gRPC build
# a separate, possibly mismatched Protobuf found beforehand.
# DR_EVT_GRPC_FETCHCONTENT is a CACHE variable so this decision is only
# computed once. Without this, every single re-configure (e.g. one
# triggered by `make` noticing CMakeLists.txt changed) would re-run
# find_package(gRPC)/find_package(Protobuf) below from scratch - even
# though, once this project has already fallen back to FetchContent,
# those searches are guaranteed to fail again every time: the _deps
# directory FetchContent populates is never a location find_package()
# searches (it's just an arbitrary build-tree path, not an installed/
# exported package location), so there is nothing new for a repeated
# search to discover. Re-running that search anyway is pure wasted
# work on every re-configure - filesystem traversal across every
# CMAKE_PREFIX_PATH entry, which is not free, particularly on a slow
# shared filesystem. Caching this once means later re-configures skip
# straight to the FetchContent branch below, with no find_package
# calls at all.
#
# If this ever needs to be re-evaluated (e.g. a working system gRPC
# becomes available later), clear the cache entry explicitly
# (`cmake -U DR_EVT_GRPC_FETCHCONTENT ..`) or start a fresh build
# directory - CACHE variables are intentionally sticky otherwise.
# DR_EVT_GRPC_FETCHCONTENT is a CACHE variable so this decision is only
# computed once - but only the FetchContent branch below can actually
# skip re-running find_package() on later configures. That asymmetry
# matters: find_package() doesn't just check whether gRPC/Protobuf
# exist, it has the side effect of creating the imported targets
# (gRPC::grpc++, protobuf::libprotobuf, etc.) the rest of this project
# links against - and unlike CACHE variables, targets are NOT persisted
# across configures, they're recreated fresh every single time. In the
# "found successfully" case, find_package() is the only mechanism that
# creates those targets, so skipping it on a later configure would
# leave them undefined - confirmed directly: caching and skipping
# unconditionally caused a real "gRPC::grpc_cpp_plugin target is
# missing" failure on a second configure. In the FetchContent case,
# skipping the (now known-to-fail-again) find_package() calls is safe,
# since FetchContent's own add_subdirectory() call further below still
# recreates its targets fresh on every configure regardless of whether
# find_package() ran first.
#
# Net effect: the expensive, guaranteed-to-fail-again find_package()
# search across every CMAKE_PREFIX_PATH entry - the actual, avoidable
# waste this caching targets - is skipped on repeat configures once
# established. The already-necessary, productive find_package() call in
# the "found successfully" case is not, since it isn't wasted work.
#
# If this ever needs to be re-evaluated (e.g. a working system gRPC
# becomes available later), clear the cache entry explicitly
# (`cmake -U DR_EVT_GRPC_FETCHCONTENT ..`) or start a fresh build
# directory - CACHE variables are intentionally sticky otherwise.
if (DR_EVT_GRPC_FETCHCONTENT)
  message(STATUS "Using cached decision from a previous configure: "
                  "building gRPC/Protobuf via FetchContent (skipping "
                  "the find_package() search, since it would only fail "
                  "again).")
else()
  find_package(gRPC CONFIG QUIET ${DR_EVT_GRPC_SEARCH_MODE})

  if (gRPC_FOUND)
    message(STATUS "Found gRPC: ${gRPC_VERSION} (gRPC_DIR: ${gRPC_DIR})")
    # HINTS ${gRPC_DIR}: try Protobuf right where gRPC's own config was
    # found first, before falling through to the default search locations
    # (still tried, since HINTS augments rather than replaces the default
    # search, unless DR_EVT_GRPC_SEARCH_MODE also excludes it above).
    find_package(Protobuf CONFIG QUIET HINTS ${gRPC_DIR} ${DR_EVT_GRPC_SEARCH_MODE})

    if (NOT Protobuf_FOUND)
      # Debian/Ubuntu's protobuf-compiler-grpc ships a CMake config, but the
      # separate libprotobuf-dev package doesn't - fall back to CMake's own
      # bundled FindProtobuf.cmake (MODULE mode), which finds the library/
      # headers/protoc directly and creates the same protobuf::libprotobuf /
      # protobuf::protoc targets CONFIG mode would have.
      find_package(Protobuf MODULE QUIET ${DR_EVT_GRPC_SEARCH_MODE})
    endif (NOT Protobuf_FOUND)
  endif (gRPC_FOUND)

  # NOTE: this check is deliberately its own, separate if-block - not an
  # else() attached to the one above. If gRPC is found but Protobuf is
  # never found by either attempt above, this must still catch that and
  # fall back to FetchContent; an earlier version of this file folded the
  # FetchContent decision into that block's own else(), which meant
  # "gRPC found, Protobuf never found" fell through with neither found
  # nor FetchContent triggered - leaving protobuf::libprotobuf undefined
  # for the rest of the project.
  if (NOT Protobuf_FOUND OR NOT gRPC_FOUND)
    set(DR_EVT_GRPC_FETCHCONTENT ON CACHE BOOL
        "Whether gRPC/Protobuf are built via FetchContent (cached: computed once)")
  endif()
endif()

if (DR_EVT_GRPC_FETCHCONTENT)
  # The generic "not found... building via FetchContent" message doesn't
  # distinguish a genuine, fresh download from FetchContent correctly
  # reusing content it already fetched in a previous configure (e.g.
  # after an interrupted build was simply resumed) - check the
  # filesystem directly, since FetchContent_MakeAvailable() itself
  # doesn't say which case applies until it's already run.
  if (EXISTS "${CMAKE_BINARY_DIR}/_deps/grpc-src/CMakeLists.txt")
    message(STATUS  "Reusing gRPC source already fetched under "
                    "${CMAKE_BINARY_DIR}/_deps/grpc-src (not re-downloading).")
  else()
    message(STATUS "gRPC and/or Protobuf not found as installed packages - "
                    "building gRPC (with its own bundled Protobuf) via "
                    "FetchContent instead. This will download gRPC's full "
                    "source tree, which can take a while the first time.")
  endif()

  include(FetchContent)
  set(ABSL_ENABLE_INSTALL ON)
  set(gRPC_INSTALL ON)

  # The Protobuf-specific compatibility concern is handled version-
  # agnostically, via try_compile() feature detection rather than
  # assuming a specific version's capabilities either way.
  #
  # CMAKE_POLICY_VERSION_MINIMUM: gRPC's source tree bundles third-party
  # dependencies (e.g. c-ares) whose CMakeLists.txt still specify a
  # cmake_minimum_required() below 3.5 - recent CMake removed
  # compatibility for that entirely. This applies policies as if 3.5 had
  # been requested for those subprojects, matching what CMake's own
  # resulting error message suggests.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

  # gRPC's bundled Abseil (its hardware-accelerated random number
  # generator, third_party/abseil-cpp/absl/random) has a real, confirmed
  # bug on Apple builds: absl/copts/AbseilConfigureCopts.cmake
  # unconditionally emits BOTH x86_64 and arm64 SIMD flags (-msse4.1,
  # -maes) on every Apple build - not just genuine universal (multi-
  # arch) ones - via `-Xarch_<arch> <flag>` pairs meant to let a single
  # multi-arch compiler invocation apply each flag only to its own
  # architecture. On a plain single-architecture build, this doesn't
  # reliably suppress the other architecture's flag, and the compiler
  # rejects it outright once it knows the actual (sole) target.
  # Confirmed directly against the actual file after a real build
  # failure - not a guess. The real fix is the PATCH_COMMAND further
  # below, which restricts that loop to the architecture(s) actually
  # being built. This block just gives that patch (and Abseil's own
  # code) an explicit CMAKE_OSX_ARCHITECTURES value to work with, rather
  # than leaving it unset and relying on a CMAKE_SYSTEM_PROCESSOR
  # fallback - without overriding an explicit user choice (e.g. a
  # genuine universal x86_64+arm64 build).
  if (APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE DR_EVT_HOST_ARCH
                     OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_OSX_ARCHITECTURES "${DR_EVT_HOST_ARCH}")
    unset(DR_EVT_HOST_ARCH)
  endif()

  # This project builds shared libraries by default (BUILD_SHARED_LIBS
  # ON at the top of CMakeLists.txt), which would otherwise propagate
  # into gRPC's own FetchContent-built dependencies (upb, Abseil, etc.).
  # On macOS, building gRPC's bundled upb as a shared library fails to
  # link ("undefined symbols") - macOS requires every symbol resolved at
  # shared-library build time, unlike Linux's .so. Force static here,
  # for gRPC's own dependencies only, and restore this project's own
  # setting immediately after so dr_evt's own libraries are unaffected.
  set(DR_EVT_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)

  # This project's own -Wall -Wextra etc. (set globally in
  # SetupCXX.cmake) would otherwise apply to gRPC's own FetchContent-
  # built source too. A trailing -w (suppress all warnings) overrides
  # the earlier flags - compilers process flags left-to-right - without
  # needing to know or parse exactly which warning flags were set
  # earlier. dr_evt's own code is unaffected: this only touches
  # CMAKE_CXX_FLAGS for the scope of gRPC's own build, restored right
  # after.
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

  # FetchContent uses add_subdirectory under the hood, so gRPC's own
  # (non-namespaced) target names are directly usable here. Alias them
  # to the same protobuf::* / gRPC::* names find_package() would have
  # produced, so the rest of this project's CMake doesn't need to know
  # or care which discovery path was actually taken.
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
# CMakeLists.txt's root-level ordering, which skips SetupProtobuf.cmake's
# own, separate discovery/build in that case.
set(DR_EVT_HAS_PROTOBUF TRUE)
