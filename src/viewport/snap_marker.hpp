#pragma once

#include <QWidget>

#include "snap/snap_types.hpp"

namespace nfcad::viewport {

/// Snap glyph and label drawn over a viewport at the snapped position (docs/snapping.md:
/// End square, Mid triangle, Centre circle, Quadrant diamond, Near hourglass, Intersection X,
/// Face corner square with dot, Point filled square, Grid cross). Green.
class SnapMarker : public QWidget {
  Q_OBJECT
 public:
  explicit SnapMarker(QWidget* parent);
  /// Shows the glyph of `type` at `px`; `label` next to it (e.g. "End", "Int (1/3)").
  void showAt(const QPointF& px, snap::SnapType type, const QString& label);
  void clear();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QPointF px_;
  snap::SnapType type_ = snap::SnapType::Point;
  QString label_;
  bool visible_ = false;
};

}  // namespace nfcad::viewport
