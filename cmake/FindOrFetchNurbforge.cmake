# Provides the target Nurbforge::core (see docs/adr/0003-nurbforge-integration.md).
#   1. installed package       find_package(Nurbforge CONFIG)
#   2. local source checkout   NFCAD_NURBFORGE_SOURCE_DIR (default: ../Nurbforge if it exists)
#   3. download                NFCAD_NURBFORGE_REPOSITORY @ NFCAD_NURBFORGE_TAG

set(NFCAD_NURBFORGE_REPOSITORY "https://github.com/PhotonPower/Nurbforge.git"
    CACHE STRING "Git repository of Nurbforge")
set(NFCAD_NURBFORGE_TAG "v0.7.0" CACHE STRING "Nurbforge git tag or commit to download")

if(NOT DEFINED NFCAD_NURBFORGE_SOURCE_DIR AND EXISTS "${PROJECT_SOURCE_DIR}/../Nurbforge/CMakeLists.txt")
  get_filename_component(_nf_default "${PROJECT_SOURCE_DIR}/../Nurbforge" ABSOLUTE)
  set(NFCAD_NURBFORGE_SOURCE_DIR "${_nf_default}")
endif()
set(NFCAD_NURBFORGE_SOURCE_DIR "${NFCAD_NURBFORGE_SOURCE_DIR}"
    CACHE PATH "Local Nurbforge checkout (empty: installed package or download)")

find_package(Nurbforge CONFIG QUIET)
if(TARGET Nurbforge::core)
  message(STATUS "Nurbforge: installed package (${Nurbforge_DIR})")
  return()
endif()

# Kernel only: no tests, examples or Python bindings of Nurbforge inside this build.
set(NF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NF_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(NF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NF_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

if(NFCAD_NURBFORGE_SOURCE_DIR)
  message(STATUS "Nurbforge: local checkout ${NFCAD_NURBFORGE_SOURCE_DIR}")
  add_subdirectory("${NFCAD_NURBFORGE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/nurbforge-build"
                   EXCLUDE_FROM_ALL SYSTEM)
else()
  message(STATUS "Nurbforge: downloading ${NFCAD_NURBFORGE_REPOSITORY} @ ${NFCAD_NURBFORGE_TAG}")
  include(FetchContent)
  # Shallow clones work for tags and branches only, not for commit hashes.
  if(NFCAD_NURBFORGE_TAG MATCHES "^[0-9a-f]+$")
    set(_nf_shallow FALSE)
  else()
    set(_nf_shallow TRUE)
  endif()
  FetchContent_Declare(nurbforge
    GIT_REPOSITORY "${NFCAD_NURBFORGE_REPOSITORY}"
    GIT_TAG "${NFCAD_NURBFORGE_TAG}"
    GIT_SHALLOW ${_nf_shallow}
    EXCLUDE_FROM_ALL
    SYSTEM)
  FetchContent_MakeAvailable(nurbforge)
endif()
