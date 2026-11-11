# UI concept

## Main window

| Area | Widget | Notes |
|---|---|---|
| Tool column (left) | `QDockWidget` → `ToolBox` | ~10 % of the window width (min 96 px, max 220 px), resizable; collapsible groups of icon buttons |
| Viewports (centre) | `ViewportGrid` | 2×2 grid of `Viewport` (QRhiWidget) with splitters; Front, Left, Top, Perspective |
| View toolbar (top) | `QToolBar` | display mode (wireframe / surfaces / shaded), standard views, zoom extents, grid on/off |
| Property panel (right) | `QDockWidget` | hidden by default, appears with a selection (M3) |
| Command line (bottom) | `CommandLine` | prompt, numeric input, history (M5) |
| Status bar | `QStatusBar` + `SnapBar` | cursor coordinates, active view, snap toggles |

The window remembers its geometry and dock layout (`QSettings`).

## Tool column

Icon buttons are arranged in **groups**. Each group has a header row (triangle + name); a click
collapses or expands it. Several groups can be open at the same time; the state is saved.
Buttons are 32 × 32 px icons (SVG, scaled for Retina) in a grid whose column count follows the
column width. A tooltip shows the name and shortcut; the status bar shows a one-line help.

| Group | Tools (planned) |
|---|---|
| Draw | point, line, polyline, rectangle, polygon |
| Curves | circle (centre/radius, 3 points), arc, helix, spline (interpolated), offset curve, fillet curve |
| Surfaces | plane, extrude, revolve, loft, sweep 1 rail, sweep 2 rails, Coons, Gordon, offset surface, fillet surface |
| Solids | box, sphere, cylinder, cone, torus, union, difference, intersection |
| Transform | move, copy, rotate, scale, mirror, array linear / grid / polar |
| Edit | trim, split, join, delete, explode |
| Analyze | distance, angle, curvature, area, volume / mass properties, intersection check |
| View | display modes, standard views, zoom extents (also in the view toolbar) |

Tool definitions are data (`src/ui/tool_catalog.cpp`): id, group, label, icon, shortcut,
status tip. Commands look up their id; the tool column is generated from the catalog.

Icons: own SVG set in `resources/icons/<group>/<tool>.svg`, monochrome, 24 px grid, drawn with
`currentColor` so that they can be coloured for light and dark themes. Missing icons fall back
to a text abbreviation, so the skeleton runs before the icon set exists.

## Viewports

| View | Looks along | Screen right / up | Projection |
|---|---|---|---|
| Front | +Y | X / Z | orthographic |
| Left | +X (from −X side) | −Y / Z | orthographic |
| Top | −Z | X / Y | orthographic |
| Perspective | iso direction (−1, −1, −1) | – | perspective (switchable to ortho) |

- The active view (last clicked) has a highlighted frame; commands and keyboard navigation act on it.
- Double-click on the view title or `Space` maximises the view; again restores the 2×2 grid.
- The view title label (top left) opens a menu: change view, projection, display mode of this view.

### Mouse and keyboard

| Action | macOS | Windows / Linux |
|---|---|---|
| Pan | middle drag, Shift + right drag, two-finger trackpad scroll | middle drag, Shift + right drag |
| Zoom (to cursor) | wheel, pinch | wheel |
| Orbit (3D view) | right drag, Alt + left drag | right drag |
| Zoom extents | `Z` `E` / double-click middle | same |
| Snap on/off | `F3` | `F3` |
| Ortho | `F8` | `F8` |
| Cycle snap candidates | `Tab` | `Tab` |
| Cancel / confirm | `Esc` / `Enter`, right-click | same |

`src/platform/` maps Cmd ↔ Ctrl and handles trackpad gestures.

## Display modes

Selected with three icons in the view toolbar (exclusive); applies to the active view, or to all
views with Shift-click.

| Mode | Faces | Edges / curves | Iso-curves |
|---|---|---|---|
| Wireframe | – | yes | yes (configurable density) |
| Surfaces | flat colour, no lighting, depth-tested | yes | no |
| Shaded | Blinn–Phong, two headlights, back faces darker | yes | no |

## Visual style

Dark theme by default (`resources/styles/dark.qss`), light theme selectable. Viewport background
is a vertical gradient; selection colour yellow, hover cyan, snap glyphs green.
