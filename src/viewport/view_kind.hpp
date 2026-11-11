#pragma once

#include <QString>

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

}  // namespace nfcad::viewport
