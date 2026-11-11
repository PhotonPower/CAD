# ctest: every plugin folder under ROOT that has a SIGNATURE must verify against the built-in
# publisher keys (docs/plugin-api.md, "Signing"). Folders without SIGNATURE are skipped.
#   cmake -DTOOL=<nfcad-plugin> -DROOT=<dir> -P VerifySignedPlugins.cmake
file(GLOB_RECURSE _sigs LIST_DIRECTORIES false "${ROOT}/*/SIGNATURE")
set(_n 0)
foreach(_sig IN LISTS _sigs)
  get_filename_component(_dir "${_sig}" DIRECTORY)
  execute_process(COMMAND "${TOOL}" verify "${_dir}" RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "signature check failed: ${_dir}")
  endif()
  math(EXPR _n "${_n} + 1")
endforeach()
message(STATUS "${_n} signed plugin(s) verified under ${ROOT}")
