# Architecture

## Layers

```
 ┌─────────────────────────────────────────────────────────────────┐
 │ app/        main(), Application, MainWindow, settings            │
 ├─────────────────────────────────────────────────────────────────┤
 │ ui/         ToolBox (collapsible groups), ToolCatalog, SnapBar,  │
 │             CommandLine, PropertyPanel, theme                    │
 │ viewport/   ViewportGrid, Viewport (QRhiWidget), Camera,         │
 │             navigation, view presets                             │
 ├─────────────────────────────────────────────────────────────────┤
 │ commands/   command framework, drawing / modelling commands      │
 │ snap/       SnapEngine (candidates, cache, exact refinement)     │
 │ pick/       ray / rectangle picking on display meshes (BVH)      │
 │ render/     Scene, GPU buffers, pipelines, shaders, picking      │
 ├─────────────────────────────────────────────────────────────────┤
 │ document/   Document, Entity, Layer, Selection, undo/redo, I/O   │
 │ tessellation/ curve / surface / trimmed-face display meshes      │
 │ kernel/     adapter to Nurbforge algorithms (I/O, modelling,     │
 │             later: snapping refinement)                          │
 ├─────────────────────────────────────────────────────────────────┤
 │ platform/   macOS / Windows / Linux specifics                    │
 └─────────────────────────────────────────────────────────────────┘
                 Nurbforge::core (C++20, no dependencies)
                 Qt 6: Core, Gui, Widgets, (Concurrent, Test)
```

Dependencies point downwards only. Rules:

- `document/`, `tessellation/`, `pick/`, `snap/` and `kernel/` do **not** use Qt Widgets (Qt Core/Gui value
  types are allowed). They are unit tested without a window.
- Nurbforge headers are used only below the widget layer: `document/` and `tessellation/` use the
  immutable value types (`nf::Vec3`, `nf::NurbsCurve`, `nf::TrimmedSurface`, `nf::Shell`, …) and
  their evaluation; algorithms that build or read geometry (STEP import, modelling commands,
  later snap refinement) are wrapped in `kernel/`. `ui/`, `viewport/` and `render/` never see
  Nurbforge types – they get display meshes.
- Only `render/` and `viewport/` use the RHI.
- Only `platform/` contains `#ifdef Q_OS_*` code (plus Objective-C++ `.mm` files on macOS).

## Data flow

```
user input ─► Viewport ─► (active) Command ─► kernel/ (Nurbforge) ─► Document (undo stack)
                 │                                                       │ changed(entity)
                 │ mouse move                                            ▼
                 └─► SnapEngine ◄── SnapCache ◄── tessellation/ (worker threads)
                         │                               │
                         ▼                               ▼
                 snap glyph overlay              render/Scene (GPU buffers) ─► 4 × Viewport
```

## Main types

| Type | Layer | Responsibility |
|---|---|---|
| `Selection` | document | selected items (entity or face / edge / vertex) and the hovered item |
| `Bvh` | tessellation | bounding volume hierarchy over triangles / segments of a display mesh |
| `pick::pick`, `pick::pick_rect` | pick | object under the cursor, window / crossing selection |
| `Document` | document | owns entities and layers, emits change signals, `QUndoStack` |
| `Entity` | document | id, layer, colour, visibility; geometry variant (`Point`, `NurbsCurve`, `NurbsSurface`, `TrimmedSurface`, `Shell`) |
| `DisplayMesh` | tessellation | vertices + normals, triangles, edge polylines, iso-curves, points, bounds of one entity |
| `TessellationService` | tessellation | worker-thread tessellation, drops outdated results |
| `Scene` | render | display model shared by all viewports: mesh, colour, visibility per entity (CPU only) |
| `SceneRenderer` | render | per-viewport GPU copy of the scene, face and line pipelines |
| `Camera` | viewport | target, view direction, up, scale (`halfHeight`), projection; project, pick ray, pan / zoom / orbit (no widgets, in `nfcad_core`) |
| `CPlane`, `build_grid` | viewport | construction plane per view and its adaptive grid (no widgets, in `nfcad_core`) |
| `LineRenderer`, `LineLayer` | render | RHI pipeline and buffers for coloured lines (grid, axes, triad) |
| `Viewport` | viewport | one QRhiWidget; camera, display mode, input → navigation or command |
| `ViewportGrid` | viewport | 2×2 layout, active view, maximise / restore |
| `ToolCatalog` | ui | data-driven list of tools (id, group, icon, shortcut) |
| `ToolBox` | ui | tool column with collapsible `ToolGroup`s |
| `SnapEngine` | snap | candidate search on the pick BVHs, cached characteristic points, exact refinement via `kernel/curve_features`, see [snapping.md](snapping.md) |
| `Command` | commands | state machine that asks for points / numbers, previews and produces entities ([commands.md](commands.md)) |
| `parse_input` | commands | typed coordinates, numbers and command names |
| `CommandController` | app | runs commands: viewports, command line, ortho, undo |

## Directory layout

```
src/
  app/ ui/ viewport/ render/ commands/ snap/ document/ tessellation/ kernel/ platform/
resources/
  icons/ shaders/ styles/ macos/ windows/ linux/
tests/          Qt Test, one executable per layer
docs/           this documentation, ADRs, build guides
cmake/          CMake helpers (warnings, Nurbforge lookup, packaging)
packaging/      dmg / installer / RPM scripts (M7, M8)
```
