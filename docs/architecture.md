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
 │ snap/       SnapEngine, snap providers, SnapCache, spatial index │
 │ render/     Scene, GPU buffers, pipelines, shaders, picking      │
 ├─────────────────────────────────────────────────────────────────┤
 │ document/   Document, Entity, Layer, Selection, undo/redo, I/O   │
 │ tessellation/ curve / surface / trimmed-face display meshes      │
 │ kernel/     adapter to Nurbforge (the only place that includes   │
 │             <nurbforge/...>)                                     │
 ├─────────────────────────────────────────────────────────────────┤
 │ platform/   macOS / Windows / Linux specifics                    │
 └─────────────────────────────────────────────────────────────────┘
                 Nurbforge::core (C++20, no dependencies)
                 Qt 6: Core, Gui, Widgets, (Concurrent, Test)
```

Dependencies point downwards only. Rules:

- `document/`, `tessellation/`, `snap/` and `kernel/` do **not** use Qt Widgets (Qt Core/Gui value
  types are allowed). They are unit tested without a window.
- Only `kernel/` includes Nurbforge headers. When the Nurbforge API changes, only this layer changes.
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
| `Document` | document | owns entities and layers, emits change signals, `QUndoStack` |
| `Entity` | document | id, layer, colour, visibility; geometry variant (`Point`, `NurbsCurve`, `NurbsSurface`, `TrimmedSurface`, `Shell`) |
| `DisplayMesh` | tessellation | vertices, triangles, edge polylines, iso-curves of one entity |
| `Scene` | render | GPU resources per entity, shared by all viewports |
| `Camera` | viewport | target, view direction, up, scale (`halfHeight`), projection; project, pick ray, pan / zoom / orbit (no widgets, in `nfcad_core`) |
| `CPlane`, `build_grid` | viewport | construction plane per view and its adaptive grid (no widgets, in `nfcad_core`) |
| `LineRenderer`, `LineLayer` | render | RHI pipeline and buffers for coloured lines (grid, axes, triad) |
| `Viewport` | viewport | one QRhiWidget; camera, display mode, input → navigation or command |
| `ViewportGrid` | viewport | 2×2 layout, active view, maximise / restore |
| `ToolCatalog` | ui | data-driven list of tools (id, group, icon, shortcut) |
| `ToolBox` | ui | tool column with collapsible `ToolGroup`s |
| `SnapEngine` | snap | candidate search and ranking, see [snapping.md](snapping.md) |
| `Command` | commands | state machine that asks for points / options and edits the document |

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
