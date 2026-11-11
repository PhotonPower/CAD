#include "ui/image_view.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

#include "results/colormap.hpp"
#include "results/result_store.hpp"

namespace nfcad::ui {

namespace {
constexpr int kLeft = 58, kRight = 86, kTop = 26, kBottom = 40;  // room for axes, title, colour bar

/// 4 to 6 "nice" tick values in [a, b].
std::vector<double> ticks(double a, double b) {
  std::vector<double> out;
  const double span = b - a;
  if (!(span > 0)) return out;
  const double raw = span / 5.0;
  const double mag = std::pow(10.0, std::floor(std::log10(raw)));
  double step = mag;
  for (double m : {1.0, 2.0, 2.5, 5.0, 10.0}) {
    if (m * mag >= raw) {
      step = m * mag;
      break;
    }
  }
  for (double t = std::ceil(a / step) * step; t <= b + 1e-9 * span; t += step) out.push_back(std::abs(t) < 1e-12 * span ? 0.0 : t);
  return out;
}
}  // namespace

// ------------------------------------------------------------------------------ view

ImageView::ImageView(const results::ResultStore* store, QWidget* parent) : QWidget(parent), store_(store) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  auto* top = new QHBoxLayout;
  chooser_ = new QComboBox(this);
  chooser_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  log_ = new QCheckBox(tr("log"), this);
  log_->setToolTip(tr("Logarithmic colour scale (faint light next to a bright spot)"));
  top->addWidget(chooser_, 1);
  top->addWidget(log_);
  layout->addLayout(top);
  canvas_ = new ImageCanvas(this);
  layout->addWidget(canvas_, 1);
  readout_ = new QLabel(this);
  readout_->setStyleSheet(QStringLiteral("color: #a8aeb8;"));
  layout->addWidget(readout_);
  connect(store_, &results::ResultStore::changed, this, &ImageView::rebuild);
  connect(chooser_, &QComboBox::currentIndexChanged, this, [this] {
    shown_ = chooser_->currentData().toULongLong();
    rebuild();
  });
  connect(log_, &QCheckBox::toggled, this, &ImageView::rebuild);
  connect(canvas_, &ImageCanvas::hovered, readout_, &QLabel::setText);
  rebuild();
}

const results::ResultItem* ImageView::current() const { return shown_ != 0 ? store_->item(shown_) : nullptr; }

void ImageView::setLogScale(bool on) { log_->setChecked(on); }

void ImageView::rebuild() {
  const auto images = store_->visibleItems(results::ResultItem::Kind::Image);
  // a new image comes to the front; otherwise the chosen one stays while it is visible
  const results::ResultId newest = images.empty() ? 0 : images.back()->id;
  const bool still = std::any_of(images.begin(), images.end(), [this](const auto* i) { return i->id == shown_; });
  if (newest != newest_ || !still) shown_ = newest;
  newest_ = newest;
  chooser_->blockSignals(true);
  chooser_->clear();
  for (const results::ResultItem* i : images) {
    QString set_name;
    for (const auto& s : store_->sets()) {
      for (const auto& it : s.items) set_name = it.id == i->id ? s.name : set_name;
    }
    chooser_->addItem(set_name.isEmpty() ? i->name : QStringLiteral("%1 – %2").arg(set_name, i->name),
                      QVariant::fromValue<qulonglong>(i->id));
  }
  chooser_->setCurrentIndex(chooser_->findData(QVariant::fromValue<qulonglong>(shown_)));
  chooser_->setVisible(images.size() > 1);
  chooser_->blockSignals(false);
  const results::ResultItem* item = current();
  canvas_->setImage(item != nullptr ? item->image : nullptr, item != nullptr ? item->name : QString(), log_->isChecked());
  readout_->setText(item != nullptr ? item->image->description : QString());
}

// ------------------------------------------------------------------------------ canvas

ImageCanvas::ImageCanvas(QWidget* parent) : QWidget(parent) {
  setMinimumSize(220, 180);
  setMouseTracking(true);
}

void ImageCanvas::setImage(std::shared_ptr<const results::Image> image, const QString& title, bool log) {
  if (image != image_) {
    image_ = std::move(image);
    resetView();  // another image: show all of it
  }
  title_ = title;
  log_ = log;
  render();
  update();
}

void ImageCanvas::render() {
  rendered_ = {};
  if (!image_ || image_->width <= 0 || image_->height <= 0) return;
  const results::Image& im = *image_;
  const results::Range r = im.effectiveRange();
  lo_ = r.min;
  hi_ = r.max;
  if (log_) {  // 4 decades below the peak, or the smallest positive value
    double positive = std::numeric_limits<double>::max();
    for (float v : im.values) {
      if (v > 0) positive = std::min(positive, static_cast<double>(v));
    }
    hi_ = std::log10(std::max(r.max, 1e-300));
    lo_ = std::max(hi_ - 4.0, std::log10(std::min(positive, std::max(r.max, 1e-300))));
    if (!(hi_ > lo_)) lo_ = hi_ - 1.0;
  }
  rendered_ = QImage(im.width, im.height, QImage::Format_RGB32);
  for (int j = 0; j < im.height; ++j) {
    auto* line = reinterpret_cast<QRgb*>(rendered_.scanLine(im.height - 1 - j));  // row 0 = y0 at the bottom
    for (int i = 0; i < im.width; ++i) {
      double v = im.value(i, j);
      if (log_) v = v > 0 ? std::log10(v) : lo_;
      const double t = std::isfinite(v) ? std::clamp((v - lo_) / (hi_ - lo_), 0.0, 1.0) : 0.0;
      line[i] = results::colormap_color(im.colormap, t).rgb();
    }
  }
}

void ImageCanvas::resetView() {
  view_ = image_ ? QRectF(QPointF(image_->x0, image_->y0), QPointF(image_->x1, image_->y1)) : QRectF();
}

QPointF ImageCanvas::toData(const QPointF& px) const {
  const QRectF box = imageRect();
  return {view_.left() + (px.x() - box.left()) / box.width() * view_.width(),
          view_.top() + (box.bottom() - px.y()) / box.height() * view_.height()};
}

QRectF ImageCanvas::imageRect() const {
  if (!image_) return {};
  const QRectF area(kLeft, kTop, width() - kLeft - kRight, height() - kTop - kBottom);
  const double aspect = view_.width() / view_.height();
  double w = area.width();
  double h = w / aspect;
  if (h > area.height()) {
    h = area.height();
    w = h * aspect;
  }
  return {area.left() + (area.width() - w) / 2, area.top() + (area.height() - h) / 2, w, h};
}

void ImageCanvas::paintEvent(QPaintEvent* /*event*/) {
  QPainter p(this);
  p.fillRect(rect(), QColor(28, 31, 36));
  p.setPen(QColor(150, 155, 165));
  if (!image_ || rendered_.isNull()) {
    p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
               tr("No image – e.g. Raytracer → Camera image"));
    return;
  }
  const results::Image& im = *image_;
  const QRectF box = imageRect();
  // the visible part of the image (zoom / pan) in source pixels; rows run from the top (y1)
  const double sx = rendered_.width() / (im.x1 - im.x0);
  const double sy = rendered_.height() / (im.y1 - im.y0);
  const QRectF source((view_.left() - im.x0) * sx, (im.y1 - view_.bottom()) * sy, view_.width() * sx,
                      view_.height() * sy);
  p.save();
  p.setClipRect(box);
  p.drawImage(box, rendered_, source);  // pixels stay pixels when enlarged (no smoothing)
  p.restore();
  p.setPen(QColor(80, 86, 96));
  p.drawRect(box);
  QFont f = font();
  f.setPointSizeF(f.pointSizeF() * 0.85);
  p.setFont(f);
  p.setPen(QColor(170, 176, 186));
  for (double t : ticks(view_.left(), view_.right())) {
    const double x = box.left() + (t - view_.left()) / view_.width() * box.width();
    p.drawLine(QPointF(x, box.bottom()), QPointF(x, box.bottom() + 4));
    p.drawText(QRectF(x - 30, box.bottom() + 5, 60, 14), Qt::AlignHCenter | Qt::AlignTop, QString::number(t, 'g', 4));
  }
  for (double t : ticks(view_.top(), view_.bottom())) {
    const double y = box.bottom() - (t - view_.top()) / view_.height() * box.height();
    p.drawLine(QPointF(box.left() - 4, y), QPointF(box.left(), y));
    p.drawText(QRectF(box.left() - 54, y - 7, 48, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(t, 'g', 4));
  }
  p.drawText(QRectF(box.left(), box.bottom() + 20, box.width(), 14), Qt::AlignHCenter, im.x_label);
  p.save();
  p.translate(10, box.center().y());
  p.rotate(-90);
  p.drawText(QRectF(-box.height() / 2, -7, box.height(), 14), Qt::AlignHCenter, im.y_label);
  p.restore();
  p.drawText(QRectF(kLeft, 4, width() - kLeft - kRight, 18), Qt::AlignLeft | Qt::AlignVCenter, title_);
  // colour bar
  const QRectF bar(width() - kRight + 14, box.top(), 14, box.height());
  for (int k = 0; k < static_cast<int>(bar.height()); ++k) {
    p.setPen(results::colormap_color(im.colormap, 1.0 - k / bar.height()));
    p.drawLine(QPointF(bar.left(), bar.top() + k), QPointF(bar.right(), bar.top() + k));
  }
  p.setPen(QColor(170, 176, 186));
  p.drawRect(bar);
  auto label = [&](double v) { return log_ ? QStringLiteral("1e%1").arg(v, 0, 'f', 1) : QString::number(v, 'g', 3); };
  p.drawText(QRectF(bar.right() + 3, bar.top() - 7, 60, 14), Qt::AlignLeft | Qt::AlignVCenter, label(hi_));
  p.drawText(QRectF(bar.right() + 3, bar.bottom() - 7, 60, 14), Qt::AlignLeft | Qt::AlignVCenter, label(lo_));
  p.drawText(QRectF(bar.left() - 6, bar.bottom() + 4, kRight - 8, 30), Qt::AlignLeft | Qt::TextWordWrap, im.unit);
}

void ImageCanvas::mouseMoveEvent(QMouseEvent* event) {
  if (!image_) return;
  const QRectF box = imageRect();
  const QPointF pos = event->position();
  if (!box.contains(pos)) {
    emit hovered(image_->description);
    return;
  }
  if (drag_) {  // panning
    const QPointF a = toData(*drag_);
    const QPointF b = toData(pos);
    view_.translate(a - b);
    drag_ = pos;
    update();
  }
  const results::Image& im = *image_;
  const QPointF d = toData(pos);
  const double x = d.x();
  const double y = d.y();
  const int i = std::clamp(static_cast<int>((x - im.x0) / (im.x1 - im.x0) * im.width), 0, im.width - 1);
  const int j = std::clamp(static_cast<int>((y - im.y0) / (im.y1 - im.y0) * im.height), 0, im.height - 1);
  emit hovered(QStringLiteral("x %1  y %2   %3 %4")
                   .arg(x, 0, 'f', 4)
                   .arg(y, 0, 'f', 4)
                   .arg(static_cast<double>(im.value(i, j)), 0, 'g', 4)
                   .arg(im.unit));
}

void ImageCanvas::mousePressEvent(QMouseEvent* event) {
  if (image_ && event->button() == Qt::LeftButton) drag_ = event->position();
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent* /*event*/) { drag_.reset(); }

void ImageCanvas::mouseDoubleClickEvent(QMouseEvent* /*event*/) {
  resetView();
  update();
}

void ImageCanvas::wheelEvent(QWheelEvent* event) {
  if (!image_) return;
  // zoom at the cursor; not beyond a few pixels, not out beyond the whole image
  const QPointF at = toData(event->position());
  const double factor = std::pow(0.85, event->angleDelta().y() / 120.0);
  const double min_w = 4.0 * (image_->x1 - image_->x0) / image_->width;
  double w = std::clamp(view_.width() * factor, min_w, image_->x1 - image_->x0);
  const double scale = w / view_.width();
  const double h = view_.height() * scale;
  view_ = QRectF(at.x() - (at.x() - view_.left()) * scale, at.y() - (at.y() - view_.top()) * scale, w, h);
  if (w >= image_->x1 - image_->x0) resetView();
  update();
  event->accept();
}

void ImageCanvas::leaveEvent(QEvent* /*event*/) {
  if (image_) emit hovered(image_->description);
}

}  // namespace nfcad::ui
