#pragma once

#include <QString>
#include <QVector3D>

#include "viewport/camera.hpp"
#include "viewport/grid.hpp"

namespace nfcad::viewport {

/// The four standard views of the 2x2 grid (docs/ui-concept.md, "Viewports").
enum class ViewKind { Front, Left, Top, Perspective };

[[nodiscard]] inline QString view_title(ViewKind kind) {
  switch (kind) {
    case ViewKind::Front: return QStringLiteral("Front (XZ)");
    case ViewKind::Left: return QStringLiteral("Left (YZ)");
    case ViewKind::Top: return QStringLiteral("Top (XY)");
    case ViewKind::Perspective: return QStringLiteral("Perspective");
  }
  return {};
}

/// Camera orientation, projection and construction plane of a standard view.
struct ViewPreset {
  QVector3D forward;  ///< viewing direction (eye → target)
  QVector3D up;       ///< screen up
  Camera::Projection projection;
  CPlane cplane;
};

[[nodiscard]] inline ViewPreset view_preset(ViewKind kind) {
  using P = Camera::Projection;
  const QVector3D x(1, 0, 0);
  const QVector3D y(0, 1, 0);
  const QVector3D z(0, 0, 1);
  switch (kind) {
    case ViewKind::Front: return {y, z, P::Orthographic, {{}, x, z}};    // right X, up Z
    case ViewKind::Left: return {x, z, P::Orthographic, {{}, -y, z}};    // right -Y, up Z
    case ViewKind::Top: return {-z, y, P::Orthographic, {{}, x, y}};     // right X, up Y
    case ViewKind::Perspective:  // south-east iso: seen from (+X, -Y, +Z)
      return {QVector3D(-1, 1, -1), z, P::Perspective, {{}, x, y}};
  }
  return {-z, y, P::Orthographic, {}};
}

}  // namespace nfcad::viewport
