#pragma once

#include <QVector3D>
#include <algorithm>
#include <limits>

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

}  // namespace nfcad::tessellation
