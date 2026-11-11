# Rendering

## Backend

Every viewport is a `QRhiWidget` (Qt ≥ 6.7, used from 6.8). Qt RHI picks the native graphics API:

| Platform | Backend | Notes |
|---|---|---|
| macOS | Metal | OpenGL is deprecated on macOS and limited to 4.1 |
| Windows 11 | Direct3D 11 (default) or 12 | |
| AlmaLinux / RHEL 10 | OpenGL ≥ 3.3 (default) or Vulkan | Wayland and X11; works with Mesa in VMs |

Shaders are written once in Vulkan-style GLSL (`resources/shaders/*.vert|frag`) and compiled to
`.qsb` at build time by `qt_add_shaders()` (SPIR-V, MSL, HLSL, GLSL).

The four viewports share one `Scene` (GPU buffers per entity, see below); each viewport has its own
camera, uniform buffer and render target. Retina / HiDPI: rendering uses the device pixel ratio;
line widths and snap glyph sizes are given in device-independent pixels.

## Display geometry

The kernel delivers exact NURBS; the GUI tessellates for display only
([ADR 0004](adr/0004-display-tessellation.md)).

| Entity | Display data | Tolerance |
|---|---|---|
| Curve | polyline, adaptive refinement on chord height and angle | chordal 0.1 % of the model size, angle 5° (setting) |
| Surface (untrimmed) | regular grid in (u, v), adaptively refined per Bézier patch | same |
| Trimmed face | grid clipped by the trim loops in parameter space, then constrained triangulation of the cut cells | same |
| Face edges | tessellated once per B-rep edge and shared by both faces → no cracks | same |
| Iso-curves | polylines at fixed u / v values, clipped by the trims | – |

Tessellation runs in a worker pool (`QtConcurrent`); a view shows bounding boxes until the data
is ready. Results are cached per entity and rebuilt only when the entity or the tolerance changes.

## Passes per frame

1. Faces (Surfaces / Shaded mode), depth write, polygon offset so that edges stay visible
2. Edges and curves (wide lines as screen-aligned quads, since line width > 1 is not portable)
3. Points and control points (instanced quads)
4. Grid and axes
5. Overlay: hover / selection highlight, snap glyph, rubber band, view title (QPainter over the RHI
   image is avoided; overlays are drawn as RHI geometry too)

Picking (M3) renders entity / sub-element ids into an `R32UI` texture of the same view and reads
back a small window around the cursor.

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
