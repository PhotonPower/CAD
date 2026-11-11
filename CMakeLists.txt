cmake_minimum_required(VERSION 3.24)

project(NurbforgeCAD
  VERSION 0.0.1
  DESCRIPTION "Desktop CAD front end for the Nurbforge NURBS kernel"
  HOMEPAGE_URL "https://github.com/PhotonPower/CAD"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NFCAD_BUILD_TESTS        "Build unit tests"                  ON)
option(NFCAD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CadWarnings)

# ---------------------------------------------------------------- dependencies
# QRhi headers (rhi/qrhi.h) belong to the semi-public RHI API and require Qt6::GuiPrivate;
# ADR 0002 accepts this. The private-module warning is therefore silenced.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg ShaderTools Concurrent)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
  # Since Qt 6.9 private modules are separate packages; before, Qt6::GuiPrivate comes with Gui.
  find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()
include(FindOrFetchNurbforge)   # -> Nurbforge::core (before AUTOMOC is switched on)
include(FetchCDT)               # -> CDT::headers

qt_standard_project_setup(REQUIRES 6.8)

# ---------------------------------------------------------------- core library (no widgets)
# Layers below the widget layer; unit tested without a window (docs/architecture.md).
add_library(nfcad_core STATIC
  src/document/document.cpp
  src/document/entity.cpp
  src/document/nfcad_io.cpp
  src/document/selection.cpp
  src/kernel/kernel_info.cpp
  src/kernel/properties.cpp
  src/kernel/step_import.cpp
  src/pick/picking.cpp
  src/snap/snap_types.cpp
  src/tessellation/bvh.cpp
  src/tessellation/display_mesh.cpp
  src/tessellation/tessellation_service.cpp
  src/tessellation/tessellator.cpp
  src/viewport/camera.cpp
  src/viewport/grid.cpp)
target_include_directories(nfcad_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Nurbforge value types appear in document/ headers (ADR 0003), hence PUBLIC.
target_link_libraries(nfcad_core
  PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent Nurbforge::core
  PRIVATE CDT::headers)
nfcad_set_warnings(nfcad_core)

# ---------------------------------------------------------------- GUI library
add_library(nfcad_gui STATIC
  src/app/file_actions.cpp
  src/app/main_window.cpp
  src/ui/tool_catalog.cpp
  src/ui/tool_box.cpp
  src/ui/layer_panel.cpp
  src/ui/property_panel.cpp
  src/ui/snap_bar.cpp
  src/render/line_renderer.cpp
  src/render/scene.cpp
  src/render/scene_renderer.cpp
  src/viewport/selection_rect.cpp
  src/viewport/viewport.cpp
  src/viewport/viewport_grid.cpp)
target_link_libraries(nfcad_gui PUBLIC nfcad_core Qt6::Widgets Qt6::GuiPrivate Qt6::Svg)
nfcad_set_warnings(nfcad_gui)

qt_add_resources(nfcad_gui "nfcad_resources"
  PREFIX "/"
  BASE resources
  FILES resources/styles/dark.qss)

# GLSL 440 -> .qsb for Metal, D3D, OpenGL, Vulkan (docs/rendering.md); served as :/shaders/*.qsb
qt_add_shaders(nfcad_gui "nfcad_shaders"
  PREFIX "/"
  BASE resources
  FILES
    resources/shaders/line.vert
    resources/shaders/line.frag
    resources/shaders/mesh.vert
    resources/shaders/mesh.frag)

# ---------------------------------------------------------------- application
qt_add_executable(NurbforgeCAD MACOSX_BUNDLE WIN32 src/app/main.cpp)
target_link_libraries(NurbforgeCAD PRIVATE nfcad_gui)
nfcad_set_warnings(NurbforgeCAD)
set_target_properties(NurbforgeCAD PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.photonpower.nurbforgecad"
  MACOSX_BUNDLE_BUNDLE_NAME "Nurbforge CAD"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
target_compile_definitions(nfcad_core PUBLIC NFCAD_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------- tools
# Demo / benchmark model: build/<preset>/tools/make_demo_model out.step [nx ny]
add_executable(make_demo_model tools/make_demo_model.cpp)
target_link_libraries(make_demo_model PRIVATE Nurbforge::core)
set_target_properties(make_demo_model PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)

# ---------------------------------------------------------------- tests
if(NFCAD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------- install
include(GNUInstallDirs)
install(TARGETS NurbforgeCAD
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
