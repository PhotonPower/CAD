#include "viewport/selection_rect.hpp"

#include <QPainter>

namespace nfcad::viewport {

SelectionRect::SelectionRect(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  hide();
}

void SelectionRect::setRect(const QRectF& rect, bool crossing) {
  rect_ = rect.normalized();
  crossing_ = crossing;
  setGeometry(parentWidget()->rect());
  update();
}

void SelectionRect::paintEvent(QPaintEvent* /*event*/) {
  QPainter p(this);
  const QColor c = crossing_ ? QColor(110, 220, 120) : QColor(90, 160, 255);
  QPen pen(c, 1.0, crossing_ ? Qt::DashLine : Qt::SolidLine);
  p.setPen(pen);
  QColor fill = c;
  fill.setAlpha(40);
  p.setBrush(fill);
  p.drawRect(rect_.adjusted(0.5, 0.5, -0.5, -0.5));
}

}  // namespace nfcad::viewport
