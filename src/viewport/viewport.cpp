#include "viewport/viewport.hpp"

#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>
#include <cmath>
#include <rhi/qrhi.h>

#include "document/selection.hpp"
#include "pick/picking.hpp"
#include "platform/rhi_backend.hpp"
#include "viewport/selection_rect.hpp"
#include "viewport/snap_marker.hpp"

namespace nfcad::viewport {

namespace {

constexpr float kDefaultExtentRadius = 10.0F;  // zoom extents of an empty model
constexpr float kWheelZoomPerNotch = 0.85F;
constexpr int kTriadMargin = 8;     // px from the bottom-left corner
constexpr int kTriadSize = 70;      // px, square
constexpr float kTriadRange = 1.45F;  // ortho half-size of the triad scene (axes have length 1)
constexpr float kPickRadiusPx = 6.0F;
constexpr double kDragThresholdPx = 4.0;

/// Selection modifiers (docs/ui-concept.md, "Selection"); Qt maps Ctrl to Cmd on macOS.
document::Selection::Mode selection_mode(Qt::KeyboardModifiers mods) {
  if ((mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier)) {
    return document::Selection::Mode::Remove;
  }
  if (mods & Qt::ShiftModifier) return document::Selection::Mode::Add;
  return document::Selection::Mode::Replace;
}

bool sub_element_mode(Qt::KeyboardModifiers mods) {
  return (mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier);
}

QString modeName(render::DisplayMode mode) {
  switch (mode) {
    case render::DisplayMode::Wireframe: return QObject::tr("Wireframe");
    case render::DisplayMode::Surfaces: return QObject::tr("Surfaces");
    case render::DisplayMode::Shaded: return QObject::tr("Shaded");
  }
  return {};
}

struct AxisStyle {
  QVector3D dir;
  float r, g, b;
  const char* name;
};
constexpr std::array<AxisStyle, 3> kAxes = {{
    {QVector3D(1, 0, 0), 0.90F, 0.35F, 0.35F, "X"},
    {QVector3D(0, 1, 0), 0.40F, 0.82F, 0.40F, "Y"},
    {QVector3D(0, 0, 1), 0.40F, 0.58F, 0.95F, "Z"},
}};

}  // namespace

Viewport::Viewport(ViewKind kind, QWidget* parent) : QRhiWidget(parent), kind_(kind) {
  setApi(platform::preferred_rhi_api());
  setSampleCount(4);  // MSAA: smoother edges and grid
  setMinimumSize(120, 90);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);  // Tab cycles snap candidates during point input

  title_ = new QLabel(this);
  title_->setObjectName(QStringLiteral("viewportTitle"));
  title_->setAttribute(Qt::WA_TransparentForMouseEvents);
  title_->setCursor(Qt::PointingHandCursor);
  title_->move(8, 6);
  rubber_ = new SelectionRect(this);
  marker_ = new SnapMarker(this);

  for (std::size_t i = 0; i < kAxes.size(); ++i) {
    auto* label = new QLabel(QString::fromLatin1(kAxes[i].name), this);
    label->setObjectName(QStringLiteral("triadLabel"));
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    label->setStyleSheet(QStringLiteral("color: rgb(%1,%2,%3); font-weight: bold;")
                             .arg(static_cast<int>(kAxes[i].r * 255))
                             .arg(static_cast<int>(kAxes[i].g * 255))
                             .arg(static_cast<int>(kAxes[i].b * 255)));
    label->adjustSize();
    triad_labels_[i] = label;
  }

  std::vector<render::LineVertex> triad;
  for (const AxisStyle& a : kAxes) {
    triad.push_back({0, 0, 0, a.r, a.g, a.b, 1.0F});
    triad.push_back({a.dir.x(), a.dir.y(), a.dir.z(), a.r, a.g, a.b, 1.0F});
  }
  triad_layer_.setVertices(std::move(triad));

  setKind(kind);
}

// ------------------------------------------------------------------------------- state

void Viewport::setKind(ViewKind kind) {
  kind_ = kind;
  const ViewPreset preset = view_preset(kind);
  camera_.setView(preset.forward, preset.up);
  camera_.setProjection(preset.projection);
  cplane_ = preset.cplane;
  camera_.setViewportSize(size());
  camera_.fitSphere({}, kDefaultExtentRadius);
  updateTitle();
  cameraChanged();
}

void Viewport::setDisplayMode(render::DisplayMode mode) {
  mode_ = mode;
  updateTitle();
  update();
}

void Viewport::setProjection(Camera::Projection projection) {
  camera_.setProjection(projection);
  updateTitle();
  cameraChanged();
}

void Viewport::setScene(const render::Scene* scene) {
  if (scene_ != nullptr) disconnect(scene_, nullptr, this, nullptr);
  scene_ = scene;
  if (scene_ != nullptr) {
    connect(scene_, &render::Scene::changed, this, [this] { update(); });
  }
  update();
}

void Viewport::setSelection(document::Selection* selection) {
  if (selection_ != nullptr) disconnect(selection_, nullptr, this, nullptr);
  selection_ = selection;
  if (selection_ != nullptr) {
    connect(selection_, &document::Selection::changed, this, [this] { update(); });
    connect(selection_, &document::Selection::hoverChanged, this, [this] { update(); });
  }
  update();
}

void Viewport::setSnapSettings(snap::SnapMask mask, bool enabled) {
  snap_mask_ = mask;
  snap_enabled_ = enabled;
}

void Viewport::setPointInput(bool on) {
  point_input_ = on;
  snaps_.clear();
  snap_px_ = QPointF(-1, -1);
  marker_->clear();
  if (on) {
    setCursor(Qt::CrossCursor);
    if (selection_ != nullptr) selection_->setHovered(std::nullopt);
  } else {
    unsetCursor();
  }
}

void Viewport::updateSnap(const QPointF& px, Qt::KeyboardModifiers mods) {
  snaps_.clear();
  snap_index_ = 0;
  snap_px_ = px;
  if (snap_enabled_ && !(mods & Qt::AltModifier) && snap_engine_ != nullptr && scene_ != nullptr) {
    snap::SnapQuery q;
    q.cursor_px = px;
    q.mask = snap_mask_;
    q.faces_occlude = mode_ != render::DisplayMode::Wireframe;
    q.grid = std::make_pair(cplane_, grid_spacing(camera_.worldPerPixel()));
    snaps_ = snap_engine_->query(scene_->pickTargets(), camera_, q);
  }
  showSnap();
}

void Viewport::showSnap() {
  if (snaps_.empty()) {
    marker_->clear();
    const auto hit = intersect_plane(camera_.pickRay(snap_px_), cplane_.origin, cplane_.normal());
    cursor_point_valid_ = hit.has_value();
    if (hit) cursor_point_ = {hit->x(), hit->y(), hit->z()};
    emit cursorMoved(hit.value_or(QVector3D()), hit.has_value());
    return;
  }
  const snap::SnapResult& r = snaps_[snap_index_ % snaps_.size()];
  QString label = QString::fromLatin1(snap::short_name(r.type));
  if (r.apparent) label += tr(" (apparent)");
  if (snaps_.size() > 1) {
    label += QStringLiteral("  %1/%2").arg(snap_index_ % snaps_.size() + 1).arg(snaps_.size());
  }
  if (const auto s = camera_.project(r.position())) marker_->showAt(*s, r.type, label);
  cursor_point_ = r.point;
  cursor_point_valid_ = true;
  emit cursorMoved(r.position(), true);
}

void Viewport::hoverPick(const QPointF& px, Qt::KeyboardModifiers mods) {
  if (selection_ == nullptr || scene_ == nullptr) return;
  pick::Query q;
  q.cursor_px = px;
  q.radius_px = kPickRadiusPx;
  q.faces = mode_ != render::DisplayMode::Wireframe;
  q.isocurves = mode_ == render::DisplayMode::Wireframe;
  q.sub_elements = sub_element_mode(mods);
  const auto hit = pick::pick(scene_->pickTargets(), camera_, q);
  selection_->setHovered(hit ? std::optional(hit->item) : std::nullopt);
}

void Viewport::clickSelect(const QPointF& px, Qt::KeyboardModifiers mods) {
  if (selection_ == nullptr || scene_ == nullptr) return;
  pick::Query q;
  q.cursor_px = px;
  q.radius_px = kPickRadiusPx;
  q.faces = mode_ != render::DisplayMode::Wireframe;
  q.isocurves = mode_ == render::DisplayMode::Wireframe;
  q.sub_elements = sub_element_mode(mods);
  const auto hit = pick::pick(scene_->pickTargets(), camera_, q);
  const auto mode = q.sub_elements ? document::Selection::Mode::Toggle : selection_mode(mods);
  if (hit) {
    selection_->apply({hit->item}, mode);
  } else if (mode == document::Selection::Mode::Replace) {
    selection_->clear();
  }
}

void Viewport::rectSelect(const QRectF& rect, bool crossing, Qt::KeyboardModifiers mods) {
  if (selection_ == nullptr || scene_ == nullptr) return;
  std::vector<document::SelectionItem> items;
  for (document::EntityId id : pick::pick_rect(scene_->pickTargets(), camera_, rect, crossing)) {
    items.push_back({id, {}});
  }
  selection_->apply(items, selection_mode(mods));
}

void Viewport::orbitBy(const QPointF& delta_px) {
  camera_.orbit(delta_px);
  cameraChanged();
}

void Viewport::zoomExtents() {
  camera_.setViewportSize(size());  // resize events of hidden widgets arrive late
  const tessellation::Box3 box = scene_ != nullptr ? scene_->bounds() : tessellation::Box3{};
  if (box.valid() && box.radius() > 0.0F) {
    camera_.fitBox(box.min, box.max);
  } else {
    camera_.fitSphere({}, kDefaultExtentRadius);
  }
  cameraChanged();
}

void Viewport::cameraChanged() {
  grid_dirty_ = true;
  updateTriadLabels();
  update();
}

void Viewport::updateTitle() {
  QString text = view_title(kind_);
  if (camera_.projection() != view_preset(kind_).projection) {
    text += camera_.projection() == Camera::Projection::Perspective ? tr(" (perspective)")
                                                                     : tr(" (parallel)");
  }
  title_->setText(text + QStringLiteral("  ·  ") + modeName(mode_) + QStringLiteral("  ▾"));
  title_->adjustSize();
}

void Viewport::updateTriadLabels() {
  const double cx = kTriadMargin + kTriadSize / 2.0;
  const double cy = height() - kTriadMargin - kTriadSize / 2.0;
  const double scale = (kTriadSize / 2.0) / kTriadRange * 1.22;  // a little beyond the tip
  for (std::size_t i = 0; i < kAxes.size(); ++i) {
    const QVector3D& d = kAxes[i].dir;
    const double sx = QVector3D::dotProduct(d, camera_.right()) * scale;
    const double sy = QVector3D::dotProduct(d, camera_.up()) * scale;
    QLabel* label = triad_labels_[i];
    label->move(static_cast<int>(cx + sx - label->width() / 2.0),
                static_cast<int>(cy - sy - label->height() / 2.0));
  }
}

// ------------------------------------------------------------------------------- rendering

void Viewport::initialize(QRhiCommandBuffer* /*cb*/) {
  lines_.initialize(rhi(), renderTarget(), sampleCount());
  scene_renderer_.initialize(rhi(), renderTarget(), sampleCount());
}

void Viewport::releaseResources() {
  grid_layer_.releaseGpu();
  triad_layer_.releaseGpu();
  lines_.release();
  scene_renderer_.release();
}

void Viewport::render(QRhiCommandBuffer* cb) {
  QRhi* r = rhi();
  camera_.setViewportSize(size());
  if (scene_ != nullptr) {
    // Keep the whole model inside the depth range.
    const tessellation::Box3 box = scene_->bounds();
    if (box.valid()) {
      camera_.setSceneRadius(std::max(box.radius() + (box.center() - camera_.target()).length(),
                                      1.0F));
    }
  }
  const QMatrix4x4 clip = r->clipSpaceCorrMatrix();

  if (grid_dirty_) {
    const float hh = camera_.halfHeight();
    const float half_view = std::max(hh * camera_.aspect(), hh);
    const bool persp = camera_.projection() == Camera::Projection::Perspective;
    const float half_extent = half_view * (persp ? 8.0F : 1.5F);
    // Grid centre: the target projected into the construction plane.
    const QVector3D n = cplane_.normal();
    const QVector3D t = camera_.target();
    const QVector3D center = t - n * QVector3D::dotProduct(t - cplane_.origin, n);
    grid_layer_.setVertices(
        build_grid(cplane_, center, grid_spacing(camera_.worldPerPixel()), half_extent));
    grid_dirty_ = false;
  }
  grid_layer_.setMvp(clip * camera_.viewProjection());

  QMatrix4x4 triad_view;
  triad_view.lookAt(-camera_.forward() * 3.0F, QVector3D(), camera_.up());
  QMatrix4x4 triad_proj;
  triad_proj.ortho(-kTriadRange, kTriadRange, -kTriadRange, kTriadRange, 0.1F, 10.0F);
  triad_layer_.setMvp(clip * triad_proj * triad_view);

  QRhiResourceUpdateBatch* updates = r->nextResourceUpdateBatch();
  lines_.prepare(grid_layer_, updates);
  lines_.prepare(triad_layer_, updates);
  if (scene_ != nullptr) {
    render::SceneRenderer::FrameParams frame;
    frame.clip_view_projection = clip * camera_.viewProjection();
    frame.view = camera_.viewMatrix();
    frame.perspective = camera_.projection() == Camera::Projection::Perspective;
    frame.world_per_pixel = camera_.worldPerPixel();
    frame.mode = mode_;
    frame.selection = selection_;
    scene_renderer_.prepare(*scene_, frame, updates);
  }

  const QSize px = renderTarget()->pixelSize();
  const float dpr = static_cast<float>(px.width()) / static_cast<float>(std::max(width(), 1));
  const QColor background = kind_ == ViewKind::Perspective ? QColor(0x3a, 0x40, 0x4a)
                                                           : QColor(0x30, 0x34, 0x3b);
  cb->beginPass(renderTarget(), background, {1.0F, 0}, updates);
  const QRhiViewport full(0, 0, static_cast<float>(px.width()), static_cast<float>(px.height()));
  lines_.draw(cb, grid_layer_, full);
  if (scene_ != nullptr) {
    scene_renderer_.drawFaces(cb, full);
    scene_renderer_.drawLines(cb, full);
  }
  // QRhiViewport origin is bottom left.
  lines_.draw(cb, triad_layer_,
              QRhiViewport(kTriadMargin * dpr, kTriadMargin * dpr, kTriadSize * dpr,
                           kTriadSize * dpr));
  cb->endPass();
  emit frameRendered();
}

// ------------------------------------------------------------------------------- input

void Viewport::mousePressEvent(QMouseEvent* event) {
  camera_.setViewportSize(size());  // resize events of hidden widgets arrive late
  emit activated(this);
  const QPointF pos = event->position();
  if (event->button() == Qt::LeftButton &&
      title_->geometry().adjusted(-4, -4, 4, 4).contains(pos.toPoint())) {
    showViewMenu();
    return;
  }
  const bool orbit_allowed = kind_ == ViewKind::Perspective;
  const Qt::KeyboardModifiers mods = event->modifiers();
  switch (event->button()) {
    case Qt::MiddleButton: drag_ = Drag::Pan; break;
    case Qt::RightButton:
      drag_ = (mods & Qt::ShiftModifier) || !orbit_allowed ? Drag::Pan : Drag::Orbit;
      break;
    case Qt::LeftButton:
      if (mods & Qt::AltModifier) {
        drag_ = orbit_allowed ? Drag::Orbit : Drag::Pan;
      } else {
        left_ = LeftAction::Pending;  // click or rectangle, decided by the movement
        press_pos_ = pos;
        return;
      }
      break;
    default: break;
  }
  if (drag_ != Drag::None) {
    last_pos_ = pos;
    drag_moved_ = false;
    setCursor(Qt::ClosedHandCursor);
    return;
  }
  QRhiWidget::mousePressEvent(event);
}

void Viewport::mouseMoveEvent(QMouseEvent* event) {
  camera_.setViewportSize(size());  // resize events of hidden widgets arrive late
  const QPointF pos = event->position();
  if (point_input_ && drag_ == Drag::None) {
    updateSnap(pos, event->modifiers());  // the left button only clicks points here
    return;
  }
  if (left_ == LeftAction::Pending &&
      QLineF(press_pos_, pos).length() > kDragThresholdPx) {
    left_ = LeftAction::Rectangle;
    rubber_->show();
    rubber_->raise();
  }
  if (left_ == LeftAction::Rectangle) {
    // left-to-right: window, right-to-left: crossing (docs/ui-concept.md)
    rubber_->setRect(QRectF(press_pos_, pos), pos.x() < press_pos_.x());
    reportCursor(pos);
    return;
  }
  if (drag_ == Drag::None && left_ == LeftAction::None) hoverPick(pos, event->modifiers());
  if (drag_ != Drag::None) {
    const QPointF delta = pos - last_pos_;
    last_pos_ = pos;
    drag_moved_ = drag_moved_ || delta.manhattanLength() > 0.0;
    marker_->clear();
    if (drag_ == Drag::Pan) {
      camera_.pan(delta);
    } else {
      camera_.orbit(delta);
    }
    cameraChanged();
  }
  reportCursor(pos);
}

void Viewport::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && left_ != LeftAction::None && point_input_) {
    const QPointF pos = event->position();
    if (pos != snap_px_) updateSnap(pos, event->modifiers());  // keep a Tab-cycled choice
    left_ = LeftAction::None;
    if (cursor_point_valid_) {
      const bool snapped = !snaps_.empty();
      emit pointPicked(cursor_point_, snapped,
                       snapped ? snaps_[snap_index_ % snaps_.size()].type : snap::SnapType::Point);
    }
    return;
  }
  if (event->button() == Qt::LeftButton && left_ != LeftAction::None) {
    const QPointF pos = event->position();
    if (left_ == LeftAction::Rectangle) {
      rubber_->hide();
      rectSelect(QRectF(press_pos_, pos), pos.x() < press_pos_.x(), event->modifiers());
    } else {
      clickSelect(pos, event->modifiers());
    }
    left_ = LeftAction::None;
    return;
  }
  if (drag_ != Drag::None) {
    const bool click = !drag_moved_;
    drag_ = Drag::None;
    if (point_input_) {
      setCursor(Qt::CrossCursor);
      if (click && event->button() == Qt::RightButton) emit pointInputFinished();
      if (click && event->button() == Qt::LeftButton) {
        // Alt + click (no drag): a point without snapping; Alt + drag still orbits / pans
        updateSnap(event->position(), event->modifiers());
        if (cursor_point_valid_) emit pointPicked(cursor_point_, false, snap::SnapType::Point);
      }
    } else {
      unsetCursor();
    }
    return;
  }
  QRhiWidget::mouseReleaseEvent(event);
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::MiddleButton) {
    zoomExtents();
    return;
  }
  QRhiWidget::mouseDoubleClickEvent(event);
}

void Viewport::wheelEvent(QWheelEvent* event) {
  camera_.setViewportSize(size());  // resize events of hidden widgets arrive late
  const QPointF pos = event->position();
  const bool zoom_modifier = (event->modifiers() & Qt::ControlModifier) != 0;  // Cmd on macOS
  const bool trackpad_scroll =
      !event->pixelDelta().isNull() && event->phase() != Qt::NoScrollPhase;
  if (trackpad_scroll && !zoom_modifier) {
    camera_.pan(QPointF(event->pixelDelta()));  // two-finger scroll pans (docs/ui-concept.md)
  } else {
    double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) steps = event->pixelDelta().y() / 50.0;
    camera_.zoomAt(pos, static_cast<float>(std::pow(kWheelZoomPerNotch, steps)));
  }
  cameraChanged();
  if (point_input_) {
    updateSnap(pos, event->modifiers());
  } else {
    reportCursor(pos);
  }
  event->accept();
}

bool Viewport::event(QEvent* event) {
  if (event->type() == QEvent::KeyPress && point_input_) {
    auto* key = static_cast<QKeyEvent*>(event);
    if ((key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) && !snaps_.empty()) {
      // Tab cycles through the snap candidates under the cursor (docs/snapping.md)
      snap_index_ = key->key() == Qt::Key_Tab ? snap_index_ + 1 : snap_index_ + snaps_.size() - 1;
      showSnap();
      return true;
    }
  }
  if (event->type() == QEvent::NativeGesture) {
    auto* gesture = static_cast<QNativeGestureEvent*>(event);
    if (gesture->gestureType() == Qt::ZoomNativeGesture) {  // trackpad pinch
  camera_.setViewportSize(size());  // resize events of hidden widgets arrive late
      camera_.zoomAt(gesture->position(), static_cast<float>(std::exp(-gesture->value())));
      cameraChanged();
      return true;
    }
    if (gesture->gestureType() == Qt::SmartZoomNativeGesture) {  // two-finger double tap
      zoomExtents();
      return true;
    }
  }
  return QRhiWidget::event(event);
}

void Viewport::leaveEvent(QEvent* event) {
  emit cursorMoved({}, false);
  marker_->clear();
  if (selection_ != nullptr && left_ == LeftAction::None) selection_->setHovered(std::nullopt);
  QRhiWidget::leaveEvent(event);
}

void Viewport::resizeEvent(QResizeEvent* event) {
  QRhiWidget::resizeEvent(event);
  camera_.setViewportSize(size());
  cameraChanged();
}

void Viewport::reportCursor(const QPointF& px) {
  const auto hit = intersect_plane(camera_.pickRay(px), cplane_.origin, cplane_.normal());
  emit cursorMoved(hit.value_or(QVector3D()), hit.has_value());
}

void Viewport::showViewMenu() {
  QMenu menu(this);
  for (ViewKind k : {ViewKind::Front, ViewKind::Left, ViewKind::Top, ViewKind::Perspective}) {
    QAction* a = menu.addAction(view_title(k), this, [this, k] { setKind(k); });
    a->setCheckable(true);
    a->setChecked(k == kind_);
  }
  menu.addSeparator();
  QAction* persp = menu.addAction(tr("Perspective projection"));
  persp->setCheckable(true);
  persp->setChecked(camera_.projection() == Camera::Projection::Perspective);
  connect(persp, &QAction::toggled, this, [this](bool on) {
    setProjection(on ? Camera::Projection::Perspective : Camera::Projection::Orthographic);
  });
  menu.addAction(tr("Zoom extents"), this, &Viewport::zoomExtents);
  menu.addSeparator();
  menu.addAction(tr("Maximize / restore  (Space)"), this, [this] { emit maximizeRequested(this); });
  menu.exec(mapToGlobal(title_->geometry().bottomLeft()));
}

}  // namespace nfcad::viewport
