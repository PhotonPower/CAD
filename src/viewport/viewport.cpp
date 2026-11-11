#include "viewport/viewport.hpp"

#include <QLabel>
#include <QMouseEvent>
#include <rhi/qrhi.h>

#include "platform/rhi_backend.hpp"

namespace nfcad::viewport {

namespace {
QString modeName(render::DisplayMode mode) {
  switch (mode) {
    case render::DisplayMode::Wireframe: return QObject::tr("Wireframe");
    case render::DisplayMode::Surfaces: return QObject::tr("Surfaces");
    case render::DisplayMode::Shaded: return QObject::tr("Shaded");
  }
  return {};
}
}  // namespace

Viewport::Viewport(ViewKind kind, QWidget* parent) : QRhiWidget(parent), kind_(kind) {
  setApi(platform::preferred_rhi_api());
  setMinimumSize(120, 90);
  title_ = new QLabel(this);
  title_->setObjectName(QStringLiteral("viewportTitle"));
  title_->setAttribute(Qt::WA_TransparentForMouseEvents);
  title_->move(8, 6);
  updateTitle();
}

void Viewport::setDisplayMode(render::DisplayMode mode) {
  mode_ = mode;
  updateTitle();
  update();
}

void Viewport::updateTitle() {
  title_->setText(view_title(kind_) + QStringLiteral("  ·  ") + modeName(mode_));
  title_->adjustSize();
}

void Viewport::initialize(QRhiCommandBuffer* /*cb*/) {
  // M1/M2: create pipelines, shared scene buffers, uniform buffer of this view's camera.
}

void Viewport::render(QRhiCommandBuffer* cb) {
  const QColor background = kind_ == ViewKind::Perspective ? QColor(0x3a, 0x40, 0x4a)
                                                           : QColor(0x30, 0x34, 0x3b);
  cb->beginPass(renderTarget(), background, {1.0F, 0});
  cb->endPass();
}

void Viewport::mousePressEvent(QMouseEvent* event) {
  emit activated(this);
  QRhiWidget::mousePressEvent(event);
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* event) {
  if (title_->geometry().adjusted(-4, -4, 4, 4).contains(event->position().toPoint())) {
    emit maximizeRequested(this);
    return;
  }
  QRhiWidget::mouseDoubleClickEvent(event);
}

}  // namespace nfcad::viewport
