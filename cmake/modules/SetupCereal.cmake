# Download the Cereal library which consists of a single header file only.
# Set up the variables CEREAL_DIR and CEREAL_HEADER.
# Create a target for download step CEREAL-download

set(DR_EVT_HAS_CEREAL TRUE)
set(CEREAL_SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/cereal)
set(CEREAL_HEADER cereal.hpp)

# A previous FetchContent build may leave CEREAL cached under external/cereal
# after that directory has been removed. Do not keep using a path that no
# longer exists: the install prefix is a fallback before a new download.
if (CEREAL AND NOT EXISTS "${CEREAL}")
  unset(CEREAL CACHE)
endif ()

set(DR_EVT_CEREAL_INSTALL_HINTS)
if (CMAKE_INSTALL_PREFIX)
  list(APPEND DR_EVT_CEREAL_INSTALL_HINTS
       "${CMAKE_INSTALL_PREFIX}/include/cereal")
endif()
find_file(CEREAL ${CEREAL_HEADER}
          HINTS ${CEREAL_SOURCE_DIR}/include/cereal
                ${DR_EVT_CEREAL_INSTALL_HINTS}
                $ENV{CEREAL_ROOT}/include/cereal
                ${CEREAL_ROOT}/include/cereal)
unset(DR_EVT_CEREAL_INSTALL_HINTS)

if (CEREAL)
  message(STATUS "Found Cereal: ${CEREAL}")
  add_custom_target(CEREAL-download)
  unset(CEREAL_DIR CACHE)
  get_filename_component(CEREAL_CEREAL_DIR ${CEREAL} DIRECTORY)
  get_filename_component(CEREAL_HEADER_DIR ${CEREAL_CEREAL_DIR} PATH CACHE)
  get_filename_component(CEREAL_DIR ${CEREAL_HEADER_DIR} PATH CACHE)
else ()
  message(STATUS "Cereal will be downloaded.")
  ExternalProject_Add(CEREAL
    GIT_REPOSITORY https://github.com/USCiLab/cereal.git
    SOURCE_DIR "${CEREAL_SOURCE_DIR}"
    LOG_DOWNLOAD ON
    STEP_TARGETS download
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
  )

  unset(CEREAL_DIR CACHE)
  set(CEREAL_DIR ${CEREAL_SOURCE_DIR})
  set(CEREAL_HEADER_DIR ${CEREAL_SOURCE_DIR}/include)

  ExternalProject_Add_StepDependencies(CEREAL build CEREAL-download)
endif ()
