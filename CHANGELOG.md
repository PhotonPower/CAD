# Changelog

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com),
versioning: [SemVer](https://semver.org) (0.x: API and file format may change).

## [Unreleased]

### Added
- M1 – viewports and camera: `Camera` (orthographic / perspective with the same scale,
  project / pick ray, pan, zoom to cursor, turntable orbit, zoom extents), standard views
  Front / Left / Top / Perspective, adaptive grid in the construction plane with world-coloured
  axes, axis triad, view menu on the view title, cursor coordinates in the status bar,
  trackpad pan / pinch zoom on macOS, line pipeline (`render/line_renderer`, shaders via
  `qt_add_shaders`), tests for camera, grid and viewport input
- Project structure, documentation (plan, UI concept, object snaps, rendering, architecture,
  ADRs 0001–0005, build guides for macOS, Windows 11 and AlmaLinux / RHEL 10)
- CMake build with Qt 6 and Nurbforge (installed package, local checkout or download)
- Main window skeleton: tool column with collapsible groups, 2×2 viewports (`QRhiWidget`),
  display-mode buttons, snap bar
- CI: macOS (required), Windows 11 and AlmaLinux 10 (prepared)

### Fixed
- Qt 6.8: `Qt6::GuiPrivate` comes with `Gui`; the separate package is only searched from Qt 6.9 on
