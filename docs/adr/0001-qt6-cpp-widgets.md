# ADR 0001 – C++20 with Qt 6 Widgets

**Context.** The GUI must run on macOS now and on Windows 11 and AlmaLinux / RHEL 10 later, and
call the C++20 Nurbforge kernel with low latency (snapping on every mouse move). Options
considered: Qt 6 Widgets (C++), Qt Quick / QML, Python + PySide6 via the Nurbforge bindings,
Dear ImGui, native toolkits per platform.

**Decision.** C++20 with Qt 6 Widgets (minimum Qt 6.8). Docks, toolbars, splitters, menus and
settings are what classic CAD programs are made of, and Widgets provides them on all three
platforms. The kernel is linked directly, without a Python layer.

**Consequences.**
- One code base and one build system (CMake) for kernel and GUI; same compiler set as Nurbforge.
- Qt is LGPL-3: dynamic linking (the default) keeps the MIT licence of this project; distributed
  bundles must ship the Qt licence texts and allow replacing the Qt libraries.
- Python scripting inside the GUI can be added later by embedding the Nurbforge Python package.
- Qt Quick was rejected because CAD-style docking and dense tool palettes are harder there; Dear
  ImGui because it lacks native menus, accessibility and HiDPI handling out of the box.
