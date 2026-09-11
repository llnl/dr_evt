# Catch2 v2 is required by the existing unit tests, which use
# <catch2/catch.hpp> and CATCH_CONFIG_MAIN.

set(DR_EVT_HAS_CATCH2 TRUE)

# Prefer an existing Catch2 v2 installation when available.
find_path(
  CATCH2_INCLUDE_DIR
  NAMES catch2/catch.hpp
  HINTS
    $ENV{CATCH2_ROOT}
    ${CATCH2_ROOT}
    ${CMAKE_INSTALL_PREFIX}
  PATH_SUFFIXES
    include
    single_include
)

if (CATCH2_INCLUDE_DIR)
  message(STATUS "Found Catch2 v2 headers: ${CATCH2_INCLUDE_DIR}")
else()
  message(STATUS "Catch2 v2 not found; fetching Catch2 v2.13.10")

  include(FetchContent)

  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v2.13.10
    GIT_SHALLOW TRUE
  )

  FetchContent_GetProperties(Catch2)
  if (NOT catch2_POPULATED)
    FetchContent_Populate(Catch2)
  endif()

  set(CATCH2_INCLUDE_DIR
      "${catch2_SOURCE_DIR}/single_include"
      CACHE PATH "Catch2 include directory")
endif()
