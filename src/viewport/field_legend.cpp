#include "viewport/field_legend.hpp"

#include <QPainter>

namespace nfcad::viewport {

namespace {
constexpr int kWidth = 150;
constexpr int kHeight = 58;
QString num(double v) { return QString::number(v, 'g', 4); }
}  // namespace

FieldLegend::FieldLegend(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  resize(kWidth, kHeight);
  hide();
}

void FieldLegend::setLegend(const QString& title, results::Colormap map, double min, double max,
                            const QString& unit) {
  title_ = title;
  map_ = map;
  min_ = min;
  max_ = max;
  unit_ = unit;
  place();
  show();
  raise();
  update();
}

void FieldLegend::clear() { hide(); }

void FieldLegend::place() {
  if (parentWidget() != nullptr) {
    move(parentWidget()->width() - kWidth - 10, parentWidget()->height() - kHeight - 10);
  }
}

void FieldLegend::paintEvent(QPaintEvent* /*event*/) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(20, 24, 28, 190));
  p.drawRoundedRect(rect(), 4, 4);
  const QRect bar(8, 24, kWidth - 16, 10);
  QLinearGradient g(bar.topLeft(), bar.topRight());
  for (int i = 0; i <= 16; ++i) g.setColorAt(i / 16.0, results::colormap_color(map_, i / 16.0));
  p.setBrush(g);
  p.drawRect(bar);
  QFont f = font();
  f.setPointSizeF(f.pointSizeF() * 0.85);
  p.setFont(f);
  p.setPen(QColor(230, 233, 238));
  const QString head = unit_.isEmpty() ? title_ : QStringLiteral("%1 [%2]").arg(title_, unit_);
  p.drawText(QRect(8, 4, kWidth - 16, 18), Qt::AlignLeft | Qt::AlignVCenter,
             p.fontMetrics().elidedText(head, Qt::ElideRight, kWidth - 16));
  p.drawText(QRect(8, 36, kWidth / 2, 18), Qt::AlignLeft | Qt::AlignVCenter, num(min_));
  p.drawText(QRect(kWidth / 2, 36, kWidth / 2 - 8, 18), Qt::AlignRight | Qt::AlignVCenter, num(max_));
}

}  // namespace nfcad::viewport
