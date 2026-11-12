# UI concept

## Main window

| Area | Widget | Notes |
|---|---|---|
| Tool column (left) | `QDockWidget` → `ToolBox` | ~10 % of the window width (min 96 px, max 220 px), resizable; collapsible groups of icon buttons |
| Viewports (centre) | `ViewportGrid` | 2×2 grid of `Viewport` (QRhiWidget) with splitters; Front, Left, Top, Perspective |
| View toolbar (top) | `QToolBar` | display mode (wireframe / surfaces / shaded), standard views, zoom extents, grid on/off |
| Property panel (right) | `QDockWidget` → `PropertyPanel` | properties of the selection (type, degrees, knots, length / area / volume) |
| Layer panel (right) | `QDockWidget` → `LayerPanel` | visibility, colour, name, new layer, move selection to a layer |
| Results (right, tab) | `QDockWidget` → `ResultsPanel` | runs and their results (rays, fields, curves) with visibility; delete, convert rays to curves |
| Plot (bottom) | `QDockWidget` → `PlotWidget` | curves of the results, live; appears with the first curve |
| Model tree (right, tab next to Layers) | `QDockWidget` → `ModelTreePanel` | groups and objects, visibility check boxes, selection in both directions, context menu: group, ungroup, hide, isolate, show all; STEP files are imported as one group |
| Command line (bottom) | `QLineEdit` + `CommandController` | prompt, typed commands, coordinates and numbers ([commands.md](commands.md)) |
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
| Perspective | iso direction (−1, 1, −1), i.e. seen from (+X, −Y, +Z) | X right-down, Y right-up, Z up | perspective (switchable to ortho) |

- The active view (last clicked) has a highlighted frame; commands and keyboard navigation act on it.
- A click on the view title (top left, "▾") opens the view menu: change to another standard view,
  perspective/parallel projection, zoom extents, maximise / restore.
- `Space` maximises the active view; again restores the 2×2 grid.
- Each view has a construction plane (Front: XZ, Left: YZ, Top and Perspective: XY) with an
  adaptive grid (minor spacing a power of ten, ≥ 12 px; every 10th line major) and the two plane
  axes in X red / Y green / Z blue. An axis triad sits in the bottom-left corner.
- The status bar shows the cursor position in the construction plane of the view under the cursor.

### Mouse and keyboard

| Action | macOS | Windows / Linux |
|---|---|---|
| Pan | middle drag, Shift + right drag, two-finger trackpad scroll | middle drag, Shift + right drag |
| Zoom (to cursor) | wheel, pinch, Cmd + two-finger scroll | wheel |
| Orbit (3D view; turntable around Z) | right drag, Alt + left drag | right drag, Alt + left drag |
| Zoom extents | `Z` `E`, double-click middle, two-finger double tap; Shift + toolbar: all views | `Z` `E`, double-click middle |
| Zoom selected (selected entities, faces, edges or vertices fill the view; without a selection: zoom extents) | `Z` `S`, toolbar, view menu; Shift + toolbar: all views | same |
| Snap on/off | `F3` | `F3` |
| Ortho (status bar button) | `F8` | `F8` |
| Cycle snap candidates | `Tab` | `Tab` |
| Cancel / confirm | `Esc` / `Enter`, right-click | same |

`src/platform/` maps Cmd ↔ Ctrl and handles trackpad gestures.

The *Node workflow* window navigates the same way: pan with middle drag, right drag (a right
click without moving opens the context menu), Alt + left drag or two-finger scroll; zoom to the
cursor with the wheel, pinch or Cmd + two-finger scroll; zoom extents with `Z` `E`, `Home`,
double-click middle or two-finger double tap; zoom selected (the selected blocks) with `Z` `S` or
the context menu. While the window has the focus these keys act on it, not on the CAD views. It
opens when a plugin sets a workflow (also again after it was closed) and from *View → Node
workflow*. Right-click on a block: *Parameters…* (also a double-click) edits the
block's parameters in a dialog generated from its type, *Replace with* exchanges it for another
type and keeps its links (each linked socket moves to the socket of the same name, else to the
only one of the same direction and kind – e.g. the sample mirror of a Linnik becomes a
`fusion.grating` sample with its Jones table). Display blocks (`display.image` at a camera, `display.plot` at a
meta block, `display.beam` as a probe at any light output) decide what *Run workflow* computes:
only the displays and what lies in front of them (like viewer nodes in Blender) – cameras and
meta blocks give no results of their own. A display shows what the run computed for it: a double-click opens their own dockable window (floating at first; drag its title bar onto
a side to dock it); *Parameters…* is in their context menu. While a workflow runs, the status bar
shows the progress per path, field point, wavelength and value (Cancel works between these steps),
the block being computed has a bright frame and "running …", and the window title says
"Node workflow – running". Results are kept across runs (up to 1 GB, the oldest go first with a
note in the plugin log): a green tick at a block means its result is up to date, a red cross that
it will be computed again. An edit marks the block and every block behind it red at once (along
the links and the blocks' internal dependencies; a phase shifter's drive passes nothing on); the
next run computes only the red blocks, *Check workflow* and every run report the state.

With RTT-Fusion R24 *Workflow from system* makes a **model**: a block shows one point per light
port (on the left where the light first comes in, on the right where it leaves or comes back),
a connection joins two ports and carries both directions: for each direction a branch uses it
shows a label with an arrow (▶ / ◀ as on the screen; the one above the band is the first-named
direction, the one below the other) and the representation – the planned one, or a fixed
transport with a star (H*); the tooltip names the blocks ("Objective A → Sample: H, transport
fixed H"); the arrows of a highlighted branch take its colour; right-click: *Transport (both
directions)*, *Transport A → B*, *Transport B → A*, *Delete connection*. After an edit the last
plan stays visible in grey ("out of date") until *Check workflow* renews it; data sockets (drive, pose, image, results) stay
directed, and a port dragged onto a display block makes an observer link. Drag from a port to a
port to connect them – a port has one connection. The **Branches** list next to the editor
shows the branches of the model: exactly these are computed (the import makes one per path of
the system). Selecting one highlights its blocks and connections; *New branch* (also in the
canvas menu) records one: click the port of a source the light leaves by, then the port it
leaves each following block by – a mirror is left by the port it came in by – then the
detector, and name it; Escape cancels. *Delete* removes the selected branch. Old workflows (socket
graphs) still open as before; *Workflow from system* turns them into a model and keeps the
edits (positions, parameters, replaced blocks with their ports, added blocks and links,
transports, own branches). *Replace with* moves the connections and branch steps of a block to
the ports of the new type (same name, else its only port).

After the import the node workflow holds the whole system (ADR Nachtrag 1): *Parameters…* of a
Raytatouille block shows a tab per element with its exact data – radii, conics, apertures,
surface poses, glasses, interactions – as the Raytatouille plugin describes them (lengths in mm,
angles in degree, a parameter's "variable" as a check box); the source shows the system's
settings (wavelengths, fields, aperture, object) and the assemblies split over blocks. Only the
values you change are written back (a value with more digits than shown stays exact).

Once the workflow holds the system it is the truth for the Raytracer plugin: *Run workflow*,
*Trace rays*, *Camera image* and *Optimise* compute with the system the workflow makes, and the
CAD view shows it – NURBS derived from the exact data, drawn again before a computation when the
workflow changed, or at once with *Update drawing*. *Optimise* writes its result into the blocks;
a plugin run that changes the workflow is undone together with its drawing (one undo step).
*Save system* exports the workflow's system as .rtt.json. Without a workflow the plugin works on
the loaded file as before.

New Raytatouille parts come from *Add block*: *Raytatouille mirror*, *beam splitter*, *lens* and
*detector* (`rtt.part.*`). Each is a real element of the system the workflow makes: give it its
pose and data in *Parameters…* (position, rotation, aperture, interaction, glass, radii), connect
its ports (mirror: front; splitter: front_a, front_b, back_a, back_b – leaving by the side the
light came in is a reflection; lens: front, back; detector: in) and lead branches through it. It
is drawn in the CAD view and computes like an imported element.

## Selection (M3)

| Action | Effect |
|---|---|
| Click | select the object under the cursor (replaces the selection); click into empty space clears |
| Shift + click | add |
| Cmd (macOS) / Ctrl + click | remove |
| Cmd / Ctrl + Shift + click | toggle a single face, edge or B-rep vertex (sub-element) |
| Drag left → right | window: objects completely inside the rectangle (solid blue frame) |
| Drag right → left | crossing: objects inside or touching the rectangle (dashed green frame) |
| `Esc` / Cmd+A / `Delete` | select none / select all / delete selected objects (undoable) |

The object under the cursor is highlighted in cyan (with Cmd/Ctrl + Shift held: the face, edge
or vertex), selected objects in yellow; the status bar names the hovered object. Pick tolerance
for curves, edges and points: 6 px. Priority: points before curves and edges before faces;
curves hidden behind faces cannot be picked in the surfaces / shaded modes; in wireframe the
iso-curves select their face. Picking works on the display meshes with a BVH ([ADR 0007](adr/0007-cpu-picking.md)).

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
