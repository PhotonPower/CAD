# Rendering

## Backend

Every viewport is a `QRhiWidget` (Qt ≥ 6.7, used from 6.8). Qt RHI picks the native graphics API:

| Platform | Backend | Notes |
|---|---|---|
| macOS | Metal | OpenGL is deprecated on macOS and limited to 4.1 |
| Windows 11 | Direct3D 11 (default) or 12 | |
| AlmaLinux / RHEL 10 | OpenGL ≥ 3.3 (default) or Vulkan | Wayland and X11; works with Mesa in VMs |

`NFCAD_RHI_BACKEND=opengl|vulkan|metal|d3d11|d3d12` overrides the choice (debugging, VMs).

Shaders are written once in Vulkan-style GLSL (`resources/shaders/*.vert|frag`) and compiled to
`.qsb` at build time by `qt_add_shaders()` (SPIR-V, MSL, HLSL, GLSL).

The four viewports share one `Scene` (GPU buffers per entity, see below); each viewport has its own
camera, uniform buffer and render target. Retina / HiDPI: rendering uses the device pixel ratio;
line widths and snap glyph sizes are given in device-independent pixels.

## Display geometry

The kernel delivers exact NURBS; the GUI tessellates for display only
([ADR 0004](adr/0004-display-tessellation.md)).

Every mesh vertex carries position, normal and the surface parameters (u, v) normalised to
[0, 1] over the face's surface domain – the texture coordinates for fields and images on faces.

| Entity | Display data | Tolerance |
|---|---|---|
| Curve | polyline, adaptive refinement on chord height and angle | chordal 0.1 % of the model size, angle 5° (setting) |
| Surface (untrimmed) | regular grid in (u, v), adaptively refined per Bézier patch | same |
| Trimmed face | constrained Delaunay triangulation ([CDT](adr/0006-cdt-for-trimmed-faces.md)) of the adaptive boundary polylines plus the interior grid points | same |
| Shell edges | tessellated once per B-rep edge (drawn once, not per face) | same |
| Iso-curves | polylines at fixed u / v values, clipped by the trims | – |

Tessellation runs in a worker pool (`TessellationService`; the faces of a shell additionally in
parallel); an entity appears as soon as its mesh is ready. Results are cached per entity and
rebuilt only when its geometry changes. Chord tolerance: 2·10⁻³ of the radius of the entity's
control-point box, angle 12°.

Known limitation (M2): neighbouring faces are tessellated independently, so tiny cracks can show
between them at close zoom. Planned fix: tessellate shared B-rep edges once and use those points
as the boundary of both faces.

## Passes per frame

1. Faces (Surfaces / Shaded mode), depth write, small depth bias
2. Edges and curves with depth test; their clip-space z is pulled towards the camera
   (`z − 5·10⁻⁵·w`) so that edges win against the faces they bound on every backend
   (wide lines as screen-aligned quads later, since line width > 1 is not portable)
3. Points and control points (instanced quads)
4. Grid and axes
5. Overlay: hover / selection highlight, snap glyph, rubber band, view title (QPainter over the RHI
   image is avoided; overlays are drawn as RHI geometry too)

Picking (M3) runs on the CPU against BVHs of the display meshes ([ADR 0007](adr/0007-cpu-picking.md)).
Highlights: selected objects get yellow edges and yellow-tinted faces, the hovered object cyan;
a selected or hovered single face is drawn a second time with an equal-depth overlay pipeline
(`LessOrEqual`, no depth write) in the tint colour, B-rep vertices as screen-sized crosses.

## Display modes

| Mode | Pass 1 shader | Pass 2 |
|---|---|---|
| Wireframe | off | curves, edges and iso-curves |
| Surfaces | unlit, entity colour | edges (black or darker entity colour) |
| Shaded | Blinn–Phong with two headlights, front/back colours | edges |

## Known pitfalls

- `QMainWindow::setUnifiedTitleAndToolBarOnMac(true)` stops `QRhiWidget` from getting a QRhi
  (Qt 6.12, macOS: the views stay empty and log "QRhiWidget: No QRhi"). Do not enable it.
- `QRhiWidget` does not choose the native backend by itself; `platform::preferred_rhi_api()` sets it.
- A depth bias on the faces alone does not keep edges visible on faces seen head-on (Metal, float
  depth); edges are therefore offset in clip space instead.
