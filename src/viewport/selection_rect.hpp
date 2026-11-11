#pragma once

#include <QWidget>

namespace nfcad::viewport {

/// Rubber band of the window / crossing selection, drawn over a viewport
/// (window: solid blue frame; crossing: dashed green frame, docs/ui-concept.md).
class SelectionRect : public QWidget {
  Q_OBJECT
 public:
  explicit SelectionRect(QWidget* parent);
  void setRect(const QRectF& rect, bool crossing);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QRectF rect_;
  bool crossing_ = false;
};

}  // namespace nfcad::viewport
