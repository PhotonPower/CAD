# Object snaps ("magnetic cursor")

While a command waits for a point, the cursor is pulled onto characteristic points of the
geometry. A green glyph marks the snap point and a tooltip names the type. Snaps work in all
four views and in every display mode.

## Snap types

| Type | Glyph | Applies to | Definition | Nurbforge |
|---|---|---|---|---|
| **Point** | small filled square | point entities, control points (optional) | the point itself | – |
| **End** | square | open curves, edges | `C(u_min)`, `C(u_max)` | `NurbsCurve` evaluation |
| **Mid** | triangle | curves, edges | point at half the **arc length** (not half the parameter) | `arc_length`, inverse by Newton |
| **Centre** | circle | circles, arcs, closed planar curves, polylines that form a closed loop | circle/arc: centre of curvature; other closed planar curves: area centroid | curvature / `properties` |
| **Quadrant** | diamond | circles, arcs | points where the circle crosses the planes through its centre parallel to the coordinate planes (0°, 90°, 180°, 270° in the world or CPlane axes) | `intersect(curve, plane)` |
| **Near** | hourglass | any curve or edge | closest point on the curve to the cursor ray | `closest_point` |
| **Intersection** | X | pairs of curves/edges, curve × face | real intersection in 3D; in orthographic views also the *apparent* intersection in the view plane | `intersect` |
| **Face corner** | square with dot | trimmed faces | vertices of the trim loops (B-rep vertices of shells) | `TrimmedSurface::boundary_curves`, `Shell` vertices |
| **Face edge** | (toggle) | faces, shells | the B-rep edges / boundary curves take part in Mid, Near, Centre and Quadrant (e.g. the centre of a drilled hole); their ends are Face corners | `ShellEdge::curve`, `boundary_curves` |
| *Grid* | small cross | construction plane | grid points (lowest priority; off by default) | – |

"Lines" in the list above means every curve: straight lines, polylines (each segment counts),
arcs, circles and free-form NURBS curves.

## Behaviour

1. **Tolerance in screen pixels** (default 10 px, setting): a candidate counts only if its screen
   position is within this radius of the cursor. This keeps the behaviour the same at every zoom.
2. **Priority** when several candidates lie within the radius:
   Point > End > Face corner > Intersection > Centre > Quadrant > Mid > Near > Grid
   (face corners are the ends of the B-rep edges; a vertical edge seen end-on in the top view
   would otherwise offer its midpoint at the same pixel before the corner).
   Within one priority the candidate closest to the cursor (in pixels) wins; ties go to the one
   closest to the camera.
3. **Tab** cycles through all candidates under the cursor.
4. **Hidden geometry:** in the Surfaces and Shaded modes, candidates hidden behind faces are
   ignored (depth test against the depth buffer of the view); in Wireframe all are used.
5. **Toggles:** each type can be switched on/off in the status bar `SnapBar`; `F3` turns snapping
   off globally; holding `Alt` suppresses snapping (Alt + click without moving places a free
   point; Alt + drag keeps orbiting / panning).
6. **Tracking (later):** hovering a snap point for 0.5 s marks it; then alignment lines along the
   axes through it are offered (polar / object tracking).

## Algorithm

```
mouse move ──► pick ray + screen position
             ──► candidate entities: BVH query on display geometry with a cone
                 (ray + pixel radius converted to world size at each depth)
             ──► per entity and enabled snap type: generate candidate points
                   cheap types (Point, End, Centre, Quadrant, corners) precomputed and cached
                   Near: closest point on the display polyline, refined with nf::closest_point
                   Intersection: pairs of candidate curves whose bounding boxes overlap
                                 in screen space → nf::intersect (3D) or 2D polyline
                                 intersection + refinement (apparent intersections)
             ──► project to screen, filter by pixel radius, depth test
             ──► sort by (priority, pixel distance, depth) ──► SnapResult
```

- Precomputed snap points (`SnapCache`) are created together with the display tessellation and
  invalidated with it.
- Exact refinement runs only for the best few candidates, so that the search stays below 2 ms per
  mouse event (target with 10 000 curves).
- The snap engine is independent of Qt widgets (only math + document), so it is unit tested
  without a window: `tests/snap/`.

## Usage (M4)

The magnetic cursor is active while a command asks for a point – in M4 the tools **Point**
(Draw group: each click creates a point) and **Distance** (Analyze group: two points, the result
appears in the status bar and the command line). Enter, right-click or Esc ends the command.
The status bar shows the exact coordinates of the snapped point.

Measured (release build, Apple Silicon): **0.34 ms** per query with 10 000 curves
(`performanceTenThousandCurves`).

## Implementation notes

- `kernel/curve_features` computes ends, mid (half arc length), centre, axis cut points
  ("quadrants") and detects circles / arcs exactly (equidistant samples, 1e-7 relative).
- Characteristic points are cached per entity on first contact (`SnapEngine::cache`);
  intersections per curve pair. The cache follows the geometry identity.
- Intersections between edges of the same face / shell are not offered: they only meet at
  vertices, which are Face corners.

## API

See `src/snap/snap_engine.hpp`: `SnapEngine::query(targets, camera, SnapQuery)` returns all
`SnapResult`s within the radius, best first (exact `SnapPoint` in double precision, type,
entities, apparent flag, screen distance, depth).
