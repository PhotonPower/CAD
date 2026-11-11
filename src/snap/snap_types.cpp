#include "snap/snap_types.hpp"

#include <algorithm>

namespace nfcad::snap {

int priority_rank(SnapType type) {
  const auto* it = std::find(kSnapPriority.begin(), kSnapPriority.end(), type);
  return static_cast<int>(it - kSnapPriority.begin());
}

std::string_view short_name(SnapType type) {
  switch (type) {
    case SnapType::Point: return "Point";
    case SnapType::End: return "End";
    case SnapType::Mid: return "Mid";
    case SnapType::Centre: return "Cen";
    case SnapType::Quadrant: return "Quad";
    case SnapType::Near: return "Near";
    case SnapType::Intersection: return "Int";
    case SnapType::FaceCorner: return "Corner";
    case SnapType::FaceEdge: return "Edge";
    case SnapType::Grid: return "Grid";
  }
  return "?";
}

SnapMask SnapMask::defaults() {
  SnapMask m;
  for (SnapType t : kSnapPriority) m.set(t, t != SnapType::Grid);
  return m;
}

}  // namespace nfcad::snap
