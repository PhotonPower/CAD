#pragma once

#include <QVector3D>
#include <cstdint>
#include <limits>
#include <vector>

namespace nfcad::tessellation {

/// Axis-aligned bounding box.
struct Box3 {
  QVector3D min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
  QVector3D max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()};

  [[nodiscard]] bool valid() const { return min.x() <= max.x(); }
  void add(const QVector3D& p) {
    min = QVector3D(std::min(min.x(), p.x()), std::min(min.y(), p.y()), std::min(min.z(), p.z()));
    max = QVector3D(std::max(max.x(), p.x()), std::max(max.y(), p.y()), std::max(max.z(), p.z()));
  }
  void add(const Box3& b) {
    if (b.valid()) {
      add(b.min);
      add(b.max);
    }
  }
  [[nodiscard]] QVector3D center() const { return (min + max) * 0.5F; }
  [[nodiscard]] float radius() const { return valid() ? (max - min).length() * 0.5F : 0.0F; }
};

/// Polyline as flat x, y, z floats.
using Polyline = std::vector<float>;

/// Display data of one entity (docs/rendering.md, "Display geometry"). Never used for modelling.
struct DisplayMesh {
  std::vector<float> vertices;         ///< interleaved x, y, z, nx, ny, nz
  std::vector<std::uint32_t> indices;  ///< triangles
  std::vector<Polyline> edges;         ///< curves, face boundaries, shell edges
  std::vector<Polyline> isocurves;     ///< wireframe only
  std::vector<float> points;           ///< point entities: x, y, z
  Box3 bounds;

  [[nodiscard]] std::size_t vertexCount() const { return vertices.size() / 6; }
  [[nodiscard]] std::size_t triangleCount() const { return indices.size() / 3; }
};

}  // namespace nfcad::tessellation
