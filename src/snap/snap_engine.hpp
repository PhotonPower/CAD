#pragma once

#include <QPointF>
#include <QVector3D>
#include <map>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "pick/picking.hpp"
#include "snap/snap_types.hpp"
#include "viewport/camera.hpp"
#include "viewport/grid.hpp"

namespace nfcad::snap {

struct SnapQuery {
  QPointF cursor_px;
  float radius_px = 10.0F;   ///< magnet radius (docs/snapping.md)
  SnapMask mask = SnapMask::defaults();
  bool faces_occlude = true; ///< surfaces / shaded mode: candidates behind faces are ignored
  /// Construction plane and minor grid spacing for Grid snaps.
  std::optional<std::pair<viewport::CPlane, float>> grid;
};

struct SnapResult {
  SnapPoint point;           ///< exact
  SnapType type = SnapType::Point;
  document::EntityId entity = 0;
  document::EntityId other = 0;  ///< second entity of an intersection
  bool apparent = false;     ///< apparent intersection (the curves only cross in this view)
  float distance_px = 0.0F;
  float depth = 0.0F;
  [[nodiscard]] QVector3D position() const {
    return {static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)};
  }
};

/// Object snaps on the display meshes, refined on the exact geometry (ADR 0005,
/// docs/snapping.md). Characteristic points per entity are computed lazily and cached
/// (`SnapCache`); intersections per curve pair. Not thread-safe (GUI thread).
class SnapEngine {
 public:
  /// All candidates within the radius, best first: priority of the type, then screen distance,
  /// then depth.
  [[nodiscard]] std::vector<SnapResult> query(const std::vector<pick::Target>& targets,
                                              const viewport::Camera& camera, const SnapQuery& q);
  /// Drops cached data of entities that are no longer in `targets` or changed geometry.
  void clear();
  [[nodiscard]] std::size_t cachedEntities() const { return caches_.size(); }

 private:
  struct Fixed {
    nf::Vec3 p;
    SnapType type;
    bool edge;  ///< belongs to a face edge (needs FaceEdge enabled)
  };
  struct Cache {
    document::GeometryPtr geometry;  // identity
    bool curve_entity = false;
    std::vector<std::vector<nf::NurbsCurve>> curves;  // per display edge polyline
    std::vector<Fixed> fixed;
  };
  const Cache& cache(const pick::Target& t);

  std::unordered_map<document::EntityId, Cache> caches_;
  using PairKey = std::tuple<document::EntityId, std::uint32_t, document::EntityId, std::uint32_t>;
  std::map<PairKey, std::vector<nf::Vec3>> intersections_;
};

}  // namespace nfcad::snap
