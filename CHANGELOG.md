# Changelog

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com),
versioning: [SemVer](https://semver.org) (0.x: API and file format may change).

## [Unreleased]

### Added
- M2 – document model and display: `Document` with entities (point, curve, surface, trimmed
  surface, shell), layers, undo/redo; STEP import (Nurbforge reader, in the background); native
  format `.nfcad` (JSON, docs/file-format.md); adaptive display tessellation in worker threads
  (CDT for trimmed faces, ADR 0006); shared `Scene` and per-view `SceneRenderer` with the display
  modes wireframe (edges + iso-curves), surfaces (flat) and shaded (Blinn–Phong, two headlights);
  MSAA; zoom extents fits the model (tight box fit in parallel views); File menu (New, Open,
  Import STEP, Save, Save As), Edit menu (Undo, Redo); command line options `file`,
  `--display-mode`, `--benchmark`; `tools/make_demo_model`; tests for tessellation, document,
  file format and STEP import
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
