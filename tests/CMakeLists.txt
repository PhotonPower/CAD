find_package(Qt6 REQUIRED COMPONENTS Test)

# One test executable per source file: tests/<layer>/test_<name>.cpp
function(nfcad_add_test name source)
  qt_add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE ${ARGN} Qt6::Test)
  nfcad_set_warnings(${name})
  add_test(NAME ${name} COMMAND ${name})
  # widget tests run without a display server in CI
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

nfcad_add_test(test_snap_types snap/test_snap_types.cpp nfcad_core)
nfcad_add_test(test_kernel kernel/test_kernel.cpp nfcad_core)
nfcad_add_test(test_tool_catalog ui/test_tool_catalog.cpp nfcad_gui)
nfcad_add_test(test_camera viewport/test_camera.cpp nfcad_core)
nfcad_add_test(test_grid viewport/test_grid.cpp nfcad_core)
nfcad_add_test(test_viewport_input viewport/test_viewport_input.cpp nfcad_gui)
nfcad_add_test(test_tessellator tessellation/test_tessellator.cpp nfcad_core)
nfcad_add_test(test_document document/test_document.cpp nfcad_core)
nfcad_add_test(test_picking pick/test_picking.cpp nfcad_core)
nfcad_add_test(test_viewport_selection viewport/test_viewport_selection.cpp nfcad_gui)
