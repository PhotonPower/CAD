# Monocypher (https://monocypher.org, CC0-1.0 or BSD-2-Clause): Ed25519 signatures of plugins
# (docs/plugins.md, "Signing"; ADR 0009). Only monocypher.c and the optional Ed25519 (SHA-512)
# part are compiled; Monocypher's own build files are not used.
set(NFCAD_MONOCYPHER_TAG "4.0.2" CACHE STRING "Monocypher git tag")
include(FetchContent)
FetchContent_Declare(monocypher
  GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
  GIT_TAG "${NFCAD_MONOCYPHER_TAG}"
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR do-not-add)   # populate only
FetchContent_MakeAvailable(monocypher)
add_library(nfcad_monocypher STATIC
  "${monocypher_SOURCE_DIR}/src/monocypher.c"
  "${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c")
target_include_directories(nfcad_monocypher SYSTEM PUBLIC
  "${monocypher_SOURCE_DIR}/src" "${monocypher_SOURCE_DIR}/src/optional")
set_target_properties(nfcad_monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
