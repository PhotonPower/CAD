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
| **Face edge** | like End / Mid / Near | trimmed faces | boundary curves of the face (outer loop and holes) take part in End, Mid and Near | `boundary_curves` |
| *Grid* | small cross | construction plane | grid points (lowest priority; off by default) | – |

"Lines" in the list above means every curve: straight lines, polylines (each segment counts),
arcs, circles and free-form NURBS curves.

## Behaviour

1. **Tolerance in screen pixels** (default 10 px, setting): a candidate counts only if its screen
   position is within this radius of the cursor. This keeps the behaviour the same at every zoom.
2. **Priority** when several candidates lie within the radius:
   Point > End > Intersection > Centre > Quadrant > Mid > Face corner > Near > Grid.
   Within one priority the candidate closest to the cursor (in pixels) wins; ties go to the one
   closest to the camera.
3. **Tab** cycles through all candidates under the cursor.
4. **Hidden geometry:** in the Surfaces and Shaded modes, candidates hidden behind faces are
   ignored (depth test against the depth buffer of the view); in Wireframe all are used.
5. **Toggles:** each type can be switched on/off in the status bar `SnapBar`; `F3` turns snapping
   off globally; holding `Alt` suppresses snapping for one click.
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

## API sketch

```cpp
namespace nfcad::snap {

enum class SnapType : std::uint16_t {
  Point = 1 << 0, End = 1 << 1, Mid = 1 << 2, Centre = 1 << 3, Quadrant = 1 << 4,
  Near = 1 << 5, Intersection = 1 << 6, FaceCorner = 1 << 7, FaceEdge = 1 << 8, Grid = 1 << 9,
};

struct SnapQuery {
  Ray pick_ray;              // world space, from the camera through the cursor
  QPointF cursor_px;         // device-independent pixels
  const ViewProjection* view;
  double radius_px = 10.0;
  SnapMask enabled;
  bool depth_test;           // false in wireframe
};

struct SnapResult {
  nf::Vec3 point;
  SnapType type;
  EntityId entity;           // and second entity for intersections
  double distance_px;
};

class SnapEngine {
 public:
  std::vector<SnapResult> query(const SnapQuery& q) const;   // sorted, best first
};

}  // namespace nfcad::snap
```
