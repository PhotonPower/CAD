# Changelog

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com),
versioning: [SemVer](https://semver.org) (0.x: API and file format may change).

## [Unreleased]

### Changed
- Renamed to **Raytatouille Fusion** (was *Nurbforge CAD*): window and menu texts, executable
  and bundle (`RaytatouilleFusion`), installer, RPM `raytatouille-fusion` (replaces
  `nurbforge-cad`), desktop entry and bundle id. Settings of the old name are taken over on the
  first start; the Windows installer keeps its AppId (updates the old installation). Unchanged:
  namespace and Python package `nfcad`, format `.nfcad`, `NFCAD_*` variables.
- GitHub repository renamed from `PhotonPower/CAD` to `PhotonPower/Raytatouille-Fusion`
  (GitHub redirects the old URLs).

### Added
- P3 – plugin host (docs/plugin-api.md): plugins in their own Python process
  (`python -m nfcad.host`), TCP channel on 127.0.0.1 with a token, JSON lines, arrays as mapped
  temporary files; Python package `nfcad` (actions, context with progress / log / cancel, model
  access, results API, geometry helpers incl. `nurbforge` conversion); Plugins menu and a
  tool-column group per plugin, settings (Python interpreter, plugin folders), progress bar with
  Cancel, plugin log dock; one undo step per run (`Document::recordApplied`); demo plugin;
  tests with a real Python process (model, results, cancel, errors, crash, foreign connection)
- P2 – result display (docs/plugins.md): `ResultStore` with runs of line sets (rays, coloured
  by a value), face fields / images (textures on faces via the mesh (u, v)) and curves; colour
  maps (viridis, inferno, plasma, coolwarm, grey) with a colour bar per view; Results dock;
  Plot dock with live curves; rays → CAD curves; *Help → Demo results on the selected object*;
  offscreen render test
- P1 – model tree and attributes (docs/plugins.md): groups (nested, undoable group / ungroup),
  visibility of objects and groups (inherited), isolate, show all; Model dock with selection
  in both directions; entity attributes (key / value, undoable, edited in the property panel,
  `sim.inactive`); `.nfcad` version 2 (groups, attributes; version 1 still read); STEP files
  import as one group; (u, v) texture coordinates in the display meshes
- M7 – Windows 11 and AlmaLinux / RHEL 10: CI jobs required; `Packages` workflow with Inno
  Setup installer (`.nfcad` association), RPM (desktop entry, icon, AppStream metadata) and
  unsigned macOS DMG; install component `app` with Qt deployment (windeployqt / macdeployqt);
  `NFCAD_RHI_BACKEND` override; THIRD_PARTY_NOTICES.md
- M6c – editing and analysis: Join (curve chains in any order, faces into shells), Explode,
  Cap planar holes (`kernel::cap_planar_holes`), Split, Trim, Intersect, Area, Volume,
  Curvature; tests incl. exact volumes of a joined box and a capped extrusion
- M6b – surfaces from curves: Spline, Offset curve, Fillet curves (two curves or all corners),
  Plane, Extrude, Revolve, Loft, Sweep 1 / 2 rails, Coons, Gordon (network sorted
  automatically), Offset surface, Fillet surfaces; tests against exact areas
- M6a – solids, transforms, export: commands Box, Sphere, Cylinder, Cone, Torus, Union,
  Difference, Intersection, Move, Copy, Rotate, Scale, Mirror, Linear / Grid / Polar array,
  Angle; selection-based commands (selection order kept for Difference); `Document::apply`
  (add / remove / replace as one undo step); File → Export STEP / STL; tests with exact volumes
- M5 – drawing commands: command framework (`Command`, `CommandController`), Point, Line,
  Polyline, Rectangle, Polygon, Circle, Circle (3 points), Arc, Helix, Distance; typed command
  names and aliases, coordinates (`x,y,z`, `x,y` in the construction plane, `@` relative,
  `d<a` polar), numbers and lengths; orange rubber-band preview; ortho (F8); Enter /
  right-click finish or repeat, Esc cancels; one undo step per result; typing in a view goes to
  the command line; docs/commands.md; tests for parser, commands and controller
- M4 – object snaps: `SnapEngine` with Point, End, Mid, Centre, Quadrant (cut points of the
  coordinate planes), Near, Intersection (real and apparent), Face corner, Face edge and Grid;
  exact refinement with Nurbforge (circle detection, arc length, closest point, curve/curve
  intersection); magnetic cursor with glyphs and labels, Tab cycles candidates, Alt suppresses,
  F3 and status bar toggles; tools Point and Distance; tests for every snap type against exact
  values, 0.34 ms per query with 10 000 curves

### Changed
- Snap priority: Face corner ranks right after End (corners are the ends of B-rep edges)
- M3 – picking and selection: CPU picking with BVHs over the display meshes (ADR 0007);
  click / Shift / Cmd-Ctrl selection, Cmd/Ctrl+Shift for faces, edges and vertices, window
  (left → right) and crossing (right → left) selection with rubber band; hover highlight (cyan)
  and selection highlight (yellow, face overlays); property panel (type, degrees, knots,
  length, area, volume); layer panel (visibility, colour, rename, new layer, move selection);
  Edit menu Delete / Select All / Select None; tests for picking, selection, layer assignment,
  properties and viewport selection
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
