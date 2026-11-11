# Project plan

Raytatouille Fusion is a desktop CAD front end for the [Nurbforge](https://github.com/PhotonPower/Nurbforge)
kernel, built to look and work like an established CAD program. This page holds the plan: goals,
scope, milestones and acceptance criteria. Progress is tracked as GitHub issues and milestones that
use the same numbers (M0, M1, ...).

## Goals

1. **CAD look and feel** – a narrow tool column on the left, a four-way viewport, display modes,
   object snaps, and keyboard and mouse behaviour that CAD users expect.
2. **Exact geometry** – all modelling goes through Nurbforge. The GUI only *displays* tessellations
   and never changes geometry itself.
3. **Cross-platform from one code base** – macOS first, then Windows 11 and AlmaLinux / RHEL 10.
   Platform-specific code is kept in `src/platform/` and stays as small as possible.

## Non-goals (for now)

- Parametric feature history or constraint sketcher (may come after M8)
- Drawing sheets, dimensioning, printing
- Plug-in system

## Screen layout

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ Menu bar                                                                     │
│ View toolbar: [Wire][Surf][Shade] | [Std views] | [Zoom all] ...             │
├─────────┬────────────────────────────────────┬───────────────────────────────┤
│▼ Draw   │  Front (XZ)                        │  Top (XY)                     │
│ ╱ ○ ⌒ ▭│                                    │                               │
│▶ Curves │                                    │                               │
│▼ Surface├────────────────────────────────────┼───────────────────────────────┤
│ ⬚ ◠ ⧉  │  Left (YZ)                         │  Perspective (3D)             │
│▶ Solids │                                    │                               │
│▶ Edit   │                                    │                               │
│▶ Analyze│                                    │                               │
├─────────┴────────────────────────────────────┴───────────────────────────────┤
│ Command line / prompt                                                        │
│ Status: X 12.500  Y 0.000  Z 3.250 │ Snap: [End][Mid][Cen][Quad][Int][Near]… │
└──────────────────────────────────────────────────────────────────────────────┘
   ~10 % width     quadrants separated by splitters; double-click maximises one
```

Details: [UI concept](ui-concept.md), [object snaps](snapping.md), [rendering](rendering.md).

## Technology (summary, see [ADRs](adr/README.md))

| Topic | Choice | ADR |
|---|---|---|
| Language / GUI | C++20, Qt 6 Widgets (≥ 6.8) | [0001](adr/0001-qt6-cpp-widgets.md) |
| 3D rendering | Qt RHI (`QRhiWidget`): Metal on macOS, D3D11/12 on Windows, OpenGL/Vulkan on Linux | [0002](adr/0002-qrhi-rendering.md) |
| Kernel link | Nurbforge C++ API (`Nurbforge::core`), via `find_package` or FetchContent | [0003](adr/0003-nurbforge-integration.md) |
| Tessellation | In the app (adaptive, chordal and angular tolerance), cached per entity; CDT for trimmed faces | [0004](adr/0004-display-tessellation.md), [0006](adr/0006-cdt-for-trimmed-faces.md) |
| Snapping | Separate snap engine, screen-space tolerance, priority list, spatial index | [0005](adr/0005-snap-engine.md) |
| Build | CMake ≥ 3.24 + Ninja, presets per platform | – |

## Milestones

Each milestone ends with a tagged release (`v0.<n>.0`) that runs on macOS. Windows and Linux CI
jobs build from M0 on but are allowed to fail until M7.

### M0 – Project skeleton ✅ (this commit)
- Repository structure, documentation, ADRs, CI (macOS required; Windows / AlmaLinux jobs prepared)
- CMake build that finds or fetches Nurbforge and Qt 6
- Main window: left tool column with collapsible groups, 2×2 viewport grid (QRhiWidget,
  background only), view toolbar with the three display modes, snap bar in the status bar
- **Done when:** `cmake --preset debug && cmake --build --preset debug` builds and the window opens on macOS.

### M1 – Viewports and camera ✅
- `Camera` (orthographic / perspective), standard views Front (XZ), Left (YZ), Top (XY), 3D iso
- Navigation: middle-drag pan, wheel zoom to cursor, right/Alt-drag orbit (3D only), zoom extents
- Grid in the construction plane, world axes, axis triad in every view
- Active viewport highlighted; `Space` or the view menu (click on the title) maximises/restores it
- Cursor coordinates in the construction plane in the status bar
- Shared scene between the four views moved to M2 (there is no geometry before M2)
- Unit tests: projection / unprojection round trip, zoom to cursor keeps the point under the cursor
- **Done when:** all four views navigate independently and stay consistent at any window size, Retina included.

### M2 – Document model and display ✅
- `Document` holding entities (point, `NurbsCurve`, `NurbsSurface`, `TrimmedSurface`, `Shell`),
  layers, colours, visibility; undo/redo via `QUndoStack`
- Display tessellation (curves: adaptive polyline; surfaces: adaptive grid clipped by trim loops)
  computed in worker threads, cached until the entity changes
- Display modes, switchable per view and globally:
  - **Wireframe** – curves, face boundaries and iso-curves
  - **Surfaces** – flat-coloured faces with boundary edges (hidden-line look)
  - **Shaded** – Phong/Blinn shading with edges
- Import: STEP (Nurbforge reader); native document format `.nfcad` ([JSON, version 1](file-format.md))
- Done: 1200-face demo model, 2.9 s read + tessellate, 60 fps orbit ([benchmarks](benchmarks.md))
- Moved to later milestones: layer panel UI (M3, together with the property panel), binary
  blocks in `.nfcad`, crack-free display mesh between neighbouring faces (shared edge
  tessellation), wide lines
- **Done when:** a STEP file with ~1 000 faces loads, displays in all three modes and navigates at ≥ 60 fps on an M1 Mac.

### M3 – Picking and selection ✅
- Picking (entity + sub-element: face, edge, vertex) on the CPU with BVHs over the display meshes ([ADR 0007](adr/0007-cpu-picking.md), instead of a GPU id buffer)
- Click, Shift-click, window (left→right) and crossing (right→left) selection, highlight on hover
- Property panel (right dock, collapsible) showing type, degree, knots, area / length
- Layer panel (from M2): visibility, colour, name, new layer, move selection to layer (undoable)
- Edit menu: delete (undoable), select all, select none
- **Done when:** selection works in all four views and in all display modes.

### M4 – Object snaps (core of this project) ✅
- Snap engine with the snap types from [snapping.md](snapping.md):
  point, end, mid, centre, quadrant (circle × axis planes), near, intersection,
  face corners, face edges (boundaries)
- Magnetic cursor: the cursor jumps to the snap point within a screen tolerance (default 10 px),
  snap glyph and tooltip ("End", "Mid", …) shown, Tab cycles candidates
- Snap toggles in the status bar, F3 turns snapping on and off, persistent settings
- Spatial index (BVH) over the display geometry for candidate search; exact refinement with
  Nurbforge (`closest_point`, `intersect`)
- First users of the magnetic cursor: the tools **Point** (Draw) and **Distance** (Analyze);
  the general command framework follows in M5
- **Done when:** every snap type has a unit test against exact geometry and the snap search takes < 2 ms per mouse move with 10 000 curves.
- Done: all snap types tested against exact values (`tests/snap/test_snap_engine.cpp`);
  0.34 ms per query with 10 000 curves (release, Apple Silicon)

### M5 – Drawing commands ✅
- Command framework (state machine, prompts in the command line, numeric input, Esc / Enter)
- Point, line, polyline, circle (centre-radius, 3 points), arc, rectangle, polygon, helix
- Construction plane (CPlane) per view; ortho mode (F8), coordinate input `x,y,z` / `@dx,dy,dz`
- Done: [commands.md](commands.md) – 10 commands incl. Distance, typed coordinates (absolute,
  relative, plane, polar), typed lengths, rubber-band preview, repeat with Enter; user-defined
  construction planes moved to a later milestone
- **Done when:** all commands work with snaps and keyboard input, with undo.

### M6a – Solids, transforms, export ✅
- Solid primitives (box, sphere, cylinder, cone, torus), Booleans (union, difference = first
  selected minus the others, intersection), transforms (move, copy, rotate, scale, mirror),
  arrays (linear, grid, polar), angle; STEP and STL export; one undo step per result

### M6b – Surfaces from curves ✅
- Spline, offset / fillet curves, plane, extrude, revolve, loft, sweep (1 / 2 rails), Coons,
  Gordon (network sorted automatically), offset / fillet surfaces – all via Nurbforge
- Moved to M6c: trim / split / join / explode, capping planar ends into solids, curvature analysis

### M6c – Editing and analysis ✅
- Join (curves, faces → shell), Explode, Cap planar holes, Split / Trim (curves by points or
  cutters, surfaces by surfaces), Intersect, Area, Volume, Curvature
- Extrude, revolve, loft, sweep (1/2 rails), Coons/Gordon, offset, fillet
- Solid primitives: box, sphere, cylinder, cone, torus; Booleans (union, difference, intersection)
- Transform: move, copy, rotate, scale, mirror, arrays (linear, grid, polar)
- Analysis: distance, angle, curvature, area/volume/mass properties
- Export: STEP, STL; mesh via gmsh (later, optional)

### M7 – Windows 11 and AlmaLinux / RHEL 10 ✅
- CI jobs required (no longer allowed to fail)
- Windows: MSVC 2022/2026, D3D11 backend, installer (WiX or Inno Setup), `windeployqt`
- AlmaLinux / RHEL 10: GCC 14, distro Qt 6 packages, Wayland and X11, OpenGL backend,
  RPM package (and optionally an AppImage/Flatpak)
- Platform differences (Cmd vs Ctrl, trackpad gestures, menu bar) handled in `src/platform/`
- **Done when:** the M1–M6 acceptance tests pass on all three platforms.
- Done: all unit tests run in CI on macOS, Windows (MSVC, Qt 6.8) and AlmaLinux 10 (GCC,
  distribution Qt) as required jobs; `Packages` workflow: Inno Setup installer, RPM (installed
  and started in the container), unsigned DMG; `NFCAD_RHI_BACKEND` selects the graphics API.
  Not verified on real Windows / RHEL desktops yet (only CI) – manual check before 1.0.

### P1–P7 – Plugins for computation kernels (done)
See [plugins.md](plugins.md): model tree, attributes, result layers and face fields, plugin
host, parameter panel, signing, reference plugin *Raytracer*, distribution.

### M8 – Release 1.0 ✅ (prepared; the release is published by the owner)
- macOS: unsigned `.dmg` (decision 2026-11-11), universal binary (arm64 + x86_64)
- User documentation (screenshots, keyboard shortcuts), example models
- Performance pass, crash reporting (local only), settings migration

## Risks

| Risk | Mitigation |
|---|---|
| QRhi API changes between Qt minor versions (semi-public API) | Pin minimum Qt 6.8, keep RHI code in `src/render/` only, CI with the newest Qt as well |
| Display tessellation of trimmed faces with gaps between neighbouring faces | Tessellate shared edges once and reuse them on both faces (watertight display mesh) |
| Snap search too slow on large models | BVH on display geometry, screen-space pre-filter, exact refinement only for the best candidates |
| AlmaLinux 10 Qt version lags behind | Check `qt6-qtbase` version in the AlmaLinux CI job from M0 on; fall back to the Qt online installer |
| Nurbforge API not yet stable (0.x) | Pin a Nurbforge tag; all kernel calls go through `src/kernel/` (adapter layer) |
