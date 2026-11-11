#pragma once

#include <QPointF>
#include <QRectF>
#include <optional>
#include <vector>

#include "document/selection.hpp"
#include "tessellation/display_mesh.hpp"
#include "viewport/camera.hpp"

namespace nfcad::pick {

/// A pickable entity: its display mesh (with pick index).
struct Target {
  document::EntityId id = 0;
  const tessellation::DisplayMesh* mesh = nullptr;
  document::GeometryPtr geometry;  ///< exact geometry (snap refinement)
};

struct Query {
  QPointF cursor_px;
  float radius_px = 6.0F;    ///< tolerance for curves, edges and points
  bool faces = true;         ///< faces are pickable (surfaces / shaded mode)
  bool isocurves = false;    ///< iso-curves select their face (wireframe mode)
  bool sub_elements = false; ///< report faces / edges / vertices (else whole entities)
};

struct Hit {
  document::SelectionItem item;  ///< sub is set only if Query::sub_elements
  QVector3D point;               ///< world position of the hit
  float depth = 0.0F;            ///< distance along the pick ray
  float distance_px = 0.0F;      ///< screen distance to the cursor (0 for face hits)
  enum class Kind { Point, Curve, Face } kind = Kind::Face;
};

/// Best object under the cursor (docs/ui-concept.md, "Selection"): points before curves and
/// edges before faces; within a kind the smallest screen distance, then the smallest depth.
/// Curves and edges hidden behind a face (in a mode with faces) are not picked.
[[nodiscard]] std::optional<Hit> pick(const std::vector<Target>& targets,
                                      const viewport::Camera& camera, const Query& query);

/// Closest face under the cursor (ray hit), with the face index if `sub_elements`.
[[nodiscard]] std::optional<Hit> pick_face(const std::vector<Target>& targets,
                                           const viewport::Camera& camera, const QPointF& cursor_px,
                                           bool sub_elements = false);

/// World length of one pixel at the farthest corner of a box (conservative pick radius).
[[nodiscard]] float farthest_world_per_pixel(const viewport::Camera& camera,
                                             const tessellation::Box3& box);

/// Window (all display geometry inside the rectangle) or crossing selection (any part inside
/// or crossing it). Rectangle in screen pixels.
[[nodiscard]] std::vector<document::EntityId> pick_rect(const std::vector<Target>& targets,
                                                        const viewport::Camera& camera,
                                                        const QRectF& rect, bool crossing);

}  // namespace nfcad::pick
