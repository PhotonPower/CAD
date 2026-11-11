#include "tessellation/display_mesh.hpp"

#include <algorithm>

namespace nfcad::tessellation {

int DisplayMesh::faceOfTriangle(std::size_t t) const {
  if (face_starts.size() < 2) return 0;
  const auto index = static_cast<std::uint32_t>(3 * t);
  const auto it = std::upper_bound(face_starts.begin(), face_starts.end(), index);
  return static_cast<int>(it - face_starts.begin()) - 1;
}

std::pair<QVector3D, QVector3D> DisplayMesh::segmentPoints(const Segment& s) const {
  const Polyline& pl = s.iso ? isocurves[s.polyline] : edges[s.polyline];
  const std::size_t i = 3 * static_cast<std::size_t>(s.point);
  return {{pl[i], pl[i + 1], pl[i + 2]}, {pl[i + 3], pl[i + 4], pl[i + 5]}};
}

void build_pick_index(DisplayMesh& m) {
  std::vector<Box3> boxes;
  boxes.reserve(m.triangleCount());
  for (std::size_t t = 0; t < m.triangleCount(); ++t) {
    Box3 b;
    for (int k = 0; k < 3; ++k) b.add(m.vertex(m.indices[3 * t + static_cast<std::size_t>(k)]));
    boxes.push_back(b);
  }
  m.triangle_bvh.build(boxes);

  m.segments.clear();
  boxes.clear();
  auto add = [&](const std::vector<Polyline>& lines, bool iso) {
    for (std::size_t p = 0; p < lines.size(); ++p) {
      const Polyline& pl = lines[p];
      for (std::size_t i = 0; i + 5 < pl.size(); i += 3) {
        Box3 b;
        b.add({pl[i], pl[i + 1], pl[i + 2]});
        b.add({pl[i + 3], pl[i + 4], pl[i + 5]});
        boxes.push_back(b);
        m.segments.push_back({static_cast<std::uint32_t>(p), static_cast<std::uint32_t>(i / 3), iso});
      }
    }
  };
  add(m.edges, false);
  add(m.isocurves, true);
  m.segment_bvh.build(boxes);
}

}  // namespace nfcad::tessellation
