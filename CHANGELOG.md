# Changelog

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com),
versioning: [SemVer](https://semver.org): from 1.0 on, the `.nfcad` format and the plugin API
(`nfcad`, API 1) change compatibly within a major version.

## [Unreleased]

### Added
- **Automotive Lighting plugin** (step 1): the facetted reflector of a clear-lens headlamp. For a
  filament lamp (H1, H3, H4, H7, H8, H9, H11, HB3, HB4 or own data), the cut-off line and the frame
  of the mirror, the reflector is cut into zones and strips whose filament images all touch the
  cut-off – each strip a family of characteristic strips of that condition (an ordinary
  differential equation). *Generate reflector* adds the facets as trimmed NURBS faces; *Filament
  images* and *Light distribution* show the images on the screen, the luminous intensity, the test
  points of UN/ECE R112 and the sharpness of the cut-off; *Show strips* draws the strips.
  ([python/plugins/automotive_lighting](python/plugins/automotive_lighting/README.md))
- **Layer stacks** in the node workflow: the FourierGrating plugin provides `fg.stack` (coatings,
  filters, mirror layers between two media, exact per ray), with an RTT-Fusion that has it.
- **FourierGrating plugin** (ADR 0010, W4, step 1): a grating's structure, materials, layers and
  incidence in the plugin panel; load and save FourierGrating configurations; *Solve* shows the
  efficiency of every order as a table and as bars; *Scan* shows the efficiency per order over λ,
  an angle, the polarisation or a sweep as curves and can write all values as CSV; *Near field*
  shows the field in the grating (|E|², |H|², power flow, components, material) as images; the
  plugin provides the grating block `fg.grating` of the node workflow, computed in its process;
  *Optimise* finds layer depths, breakpoints or periods for an objective Σ weight · efficiency with
  FourierGrating's exact gradients (live curve, the optimum applied to the panel); *Set Littrow
  angle*; the order table as CSV and the near field as `.npz`; *Layer from CAD bodies* turns
  drawn bodies into a grating layer (FourierGrating's STEP slicer) and *Check CAD layers* shows its
  bands and findings. Plugins can write entities as STEP (`ctx.export_step`). *Estimate* gives
  the size and rough memory of a computation; *Scan* and *Optimise* warn when it nears the RAM.
- Plugins: structured parameters (lists, tables, JSON), result tables and bar charts,
  `ctx.set_params` (ADR 0010, W4a).
- **Path Tracer plugin**: light transport on the CAD model – surfaces, bodies and points as light
  sources (flux, emission cone, side), materials on the others (absorber, diffuse with
  transmission, mirror, glossy, glass with absorption); *Irradiance* as a texture on the faces
  (W/m²) with a table per face, *Camera image* (radiance of a pinhole camera) in the Image window;
  light tracing, path tracing and bidirectional path tracing as one estimator with multiple
  importance sampling; *Show light paths* and *Show camera* as previews. NumPy only.
  Material `detector`: a face the light passes unchanged, only to measure the irradiance there.
- **Camera image** (Raytracer): the irradiance on the last surface of each path from many rays
  (default 200 000 per field and wavelength, millions possible), square pixels over the
  sensor, shown in the new **Image** window and optionally on the surface in 3D. The quick
  irradiance of *Trace rays* is now optional (off).
- Documents are saved as **`.rttf`** (Raytatouille Fusion file); `.nfcad` files from before
  1.0 still open (same content), *Save As* offers them as `.rttf`. Windows associates both.
- Raytracer: the number of rays is set directly (*Rays (drawn)*, *Rays (quick irradiance)*,
  per field and wavelength) instead of pupil rings.
- *Plugins → Uninstall plugin* lists every plugin; those from a plugin folder are shown greyed
  out with the folder they come from.
- **Image** window and result type: 2D images with physical axes, colour bar, linear / log
  scale, read-out under the cursor, zoom and pan; plugin API `ctx.image`.

### Changed
- Raytracer plugin: the irradiance comes from Raytatouille (`rt.analysis.irradiance_map`, #379)
  on the last surface of the path – incident weight per projected cell area, uncounted rays
  in the plugin log – instead of the plugin's own binning; it needs a Raytatouille with #379.

### Added
- Plugin parameter type `Param.files` (a list of files with *Add…* / *Remove*); the Raytracer's
  *Glass catalogues* use it – catalogues are chosen with the file dialog instead of typing
  `;`-separated paths (documents with the old text are still read).

### Fixed
- Crash when hiding results (e.g. the rays of one arm) or a layer with its check box: the panel
  rebuilt its list while Qt was still handling the click. The lists also keep their scroll
  position now.
- Raytracer: loading some systems failed with "Singular matrix" (a rounding error put the
  last interpolation point outside the parameter range); steep, nearly hemispherical lenses
  were drawn up to 0.1 mm off – rotationally symmetric surfaces are now surfaces of revolution
  (all surfaces of the tested objectives and the Linnik within 0.1 µm).
- Raytracer: *Trace rays* traces every path of the system (or the ones listed under *Paths*),
  one line set per path, *Colour by → path*; paths ending on the same surface add up in its
  irradiance.
- Raytracer: *Trace rays* works for folded / tilted paths (beam splitter, Linnik) whose stop is
  the first surface: the plugin builds the start rays itself, identical to Raytatouille's
  `make_rays` where both work.
- Plugin panel in a narrow dock: rows wrap (editor below its label), the file chooser button is
  no longer cut off, an empty file field says what to do.
- Plugin errors the user can fix (`nfcad.UserError`, e.g. no system file chosen, missing glass
  catalogue) are shown as a message instead of a Python traceback in the log.

## [1.0.0] - 2026-11-11

First release: a CAD front end for Nurbforge with four views, object snaps, drawing, solids,
STEP / STL, results on the model – and plugins for computation kernels, starting with the
Raytatouille ray tracer. Packages: Windows 11 installer, AlmaLinux / RHEL 10 RPM, macOS disk
image (universal, unsigned).

### Added
- **Release 1.0 (M8):** macOS build as a universal binary (arm64 + x86_64); local crash
  reports (written on a crash, pointed to at the next start, never sent); versioned settings
  migration; user guide with screenshots and shortcuts (`docs/user-guide.md`); example
  models (`examples/`); a tag `v*` builds the packages and drafts a GitHub release.
- A plugin run that fills an empty document zooms all views to its result.

### Fixed
- Closing the window with an undo history could call into the half-destroyed window (crash in
  debug builds, undefined behaviour in release builds).
- Model tree: names get the column width instead of the type column.

### Added
- **Plugin distribution (P7):** `.nfplugin` packages (zip of the plugin folder with its
  signature; `nfcad-plugin pack`), *Plugins → Install plugin…* / *Uninstall plugin* into the
  folder for installed plugins (also by opening the file); requirements in `plugin.json`
  (`requires`) with *Check environment* and *Create environment…* in the plugin settings;
  manual update check *Help → Check for updates…* (GitHub releases, `update_url` of plugins).
- **Raytracer plugin (P6)**, `python/plugins/raytracer`: loads Raytatouille systems
  (`.rtt.json`) as NURBS faces (bridge rtt → NURBS: interpolated sag within a tolerance,
  exact trims, lens rims, stop ring) in a model tree following the rtt node tree; traces rays
  (line set) and the irradiance on the image surface (face field); optimises with live
  surface updates and a merit curve; saves the system. Undo in CAD follows the system revision.
- Plugin API: `ctx.remove`, `ctx.groups`, `ctx.add_group`, `ctx.remove_group`, `group=` in
  `ctx.add` – all part of the run's single undo step.
- **Plugin signatures (P5):** only plugins signed with the built-in publisher key load
  (Ed25519 via Monocypher; file `SIGNATURE`); they run from a private, checked copy. Developer
  mode loads unsigned plugins, marked, with a red banner. Tool `nfcad-plugin` (keygen, sign,
  verify); test `signed_plugins` checks signed plugins in the repository. ADR 0009.
- **Plugin parameter panel (P4):** plugins describe their parameters with `nfcad.Param`
  (number with unit and limits, integer, boolean, choice, text, file, number list, point,
  model object); CAD builds a form in the new *Plugin* dock with the plugin's action buttons.
  Points are picked in the views with object snaps, objects taken from the selection. The
  values are saved per plugin in the `.nfcad` document and arrive in `ctx.params` as Python
  types.

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
