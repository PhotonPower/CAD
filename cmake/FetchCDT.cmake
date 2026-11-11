# Constrained Delaunay triangulation (https://github.com/artem-ogre/CDT, MPL-2.0), header-only.
# Used by src/tessellation/ for trimmed faces (docs/adr/0006-cdt-for-trimmed-faces.md).
# Only the headers are used; CDT's own CMakeLists is not added (it would lower the C++ standard
# to C++11 on AppleClang).
set(NFCAD_CDT_TAG "1.4.4" CACHE STRING "CDT git tag")
include(FetchContent)
FetchContent_Declare(cdt
  GIT_REPOSITORY https://github.com/artem-ogre/CDT.git
  GIT_TAG "${NFCAD_CDT_TAG}"
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR do-not-add)   # populate only
FetchContent_MakeAvailable(cdt)
add_library(nfcad_cdt INTERFACE)
target_include_directories(nfcad_cdt SYSTEM INTERFACE "${cdt_SOURCE_DIR}/CDT/include")
add_library(CDT::headers ALIAS nfcad_cdt)
