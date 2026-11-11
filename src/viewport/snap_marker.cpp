#include "viewport/snap_marker.hpp"

#include <QPainter>
#include <QPainterPath>

namespace nfcad::viewport {

namespace {
constexpr double kHalf = 6.0;  // glyph half size, px
const QColor kSnapColor(80, 230, 110);
}  // namespace

SnapMarker::SnapMarker(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  hide();
}

void SnapMarker::showAt(const QPointF& px, snap::SnapType type, const QString& label) {
  px_ = px;
  type_ = type;
  label_ = label;
  visible_ = true;
  setGeometry(parentWidget()->rect());
  show();
  raise();
  update();
}

void SnapMarker::clear() {
  if (!visible_) return;
  visible_ = false;
  hide();
}

void SnapMarker::paintEvent(QPaintEvent* /*event*/) {
  if (!visible_) return;
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  QPen pen(kSnapColor, 2.0);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  const QPointF c = px_;
  const double h = kHalf;
  using snap::SnapType;
  switch (type_) {
    case SnapType::Point:
      p.setBrush(kSnapColor);
      p.drawRect(QRectF(c.x() - h * 0.6, c.y() - h * 0.6, h * 1.2, h * 1.2));
      break;
    case SnapType::End:
      p.drawRect(QRectF(c.x() - h, c.y() - h, 2 * h, 2 * h));
      break;
    case SnapType::FaceCorner:
      p.drawRect(QRectF(c.x() - h, c.y() - h, 2 * h, 2 * h));
      p.setBrush(kSnapColor);
      p.drawEllipse(c, 1.8, 1.8);
      break;
    case SnapType::Mid: {
      QPainterPath tri;
      tri.moveTo(c.x(), c.y() - h);
      tri.lineTo(c.x() + h, c.y() + h * 0.8);
      tri.lineTo(c.x() - h, c.y() + h * 0.8);
      tri.closeSubpath();
      p.drawPath(tri);
      break;
    }
    case SnapType::Centre:
      p.drawEllipse(c, h, h);
      break;
    case SnapType::Quadrant: {
      QPainterPath d;
      d.moveTo(c.x(), c.y() - h);
      d.lineTo(c.x() + h, c.y());
      d.lineTo(c.x(), c.y() + h);
      d.lineTo(c.x() - h, c.y());
      d.closeSubpath();
      p.drawPath(d);
      break;
    }
    case SnapType::Near: {
      QPainterPath g;  // hourglass
      g.moveTo(c.x() - h, c.y() - h);
      g.lineTo(c.x() + h, c.y() - h);
      g.lineTo(c.x() - h, c.y() + h);
      g.lineTo(c.x() + h, c.y() + h);
      g.closeSubpath();
      p.drawPath(g);
      break;
    }
    case SnapType::Intersection:
      p.drawLine(QPointF(c.x() - h, c.y() - h), QPointF(c.x() + h, c.y() + h));
      p.drawLine(QPointF(c.x() - h, c.y() + h), QPointF(c.x() + h, c.y() - h));
      break;
    case SnapType::Grid:
    case SnapType::FaceEdge:
      pen.setWidthF(1.5);
      p.setPen(pen);
      p.drawLine(QPointF(c.x() - h, c.y()), QPointF(c.x() + h, c.y()));
      p.drawLine(QPointF(c.x(), c.y() - h), QPointF(c.x(), c.y() + h));
      break;
  }
  if (!label_.isEmpty()) {
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    p.setFont(f);
    const QPointF at(c.x() + h + 6, c.y() - h - 3);
    const QRectF box = p.fontMetrics().boundingRect(label_).toRectF().translated(at).adjusted(-3, -1, 3, 1);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 24, 28, 200));
    p.drawRoundedRect(box, 3, 3);
    p.setPen(kSnapColor);
    p.drawText(at, label_);
  }
}

}  // namespace nfcad::viewport
