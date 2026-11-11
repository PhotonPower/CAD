#pragma once

#include <QPointF>
#include <QRhiWidget>
#include <array>

#include "render/display_mode.hpp"
#include "render/line_renderer.hpp"
#include "render/scene_renderer.hpp"
#include "viewport/camera.hpp"
#include "viewport/grid.hpp"
#include "viewport/view_kind.hpp"

class QLabel;

namespace nfcad::viewport {

/// One 3D view (QRhiWidget: Metal / D3D / OpenGL·Vulkan, docs/rendering.md).
/// Owns its camera and construction plane; draws grid, world axes and an axis triad (M1).
/// Navigation: docs/ui-concept.md, "Mouse and keyboard".
class Viewport : public QRhiWidget {
  Q_OBJECT
 public:
  explicit Viewport(ViewKind kind, QWidget* parent = nullptr);

  [[nodiscard]] ViewKind kind() const { return kind_; }
  /// Switches to another standard view (camera orientation, projection, construction plane).
  void setKind(ViewKind kind);

  [[nodiscard]] render::DisplayMode displayMode() const { return mode_; }
  void setDisplayMode(render::DisplayMode mode);

  [[nodiscard]] const Camera& camera() const { return camera_; }
  [[nodiscard]] const CPlane& cplane() const { return cplane_; }
  void setProjection(Camera::Projection projection);

  /// The shared display model (owned by the main window); may be null.
  void setScene(const render::Scene* scene);

  /// Orbits like a mouse drag by `delta_px` (benchmark, scripted views).
  void orbitBy(const QPointF& delta_px);

  /// Shows all visible geometry (a default region around the origin if there is none).
  void zoomExtents();

 signals:
  void activated(nfcad::viewport::Viewport* view);
  void maximizeRequested(nfcad::viewport::Viewport* view);
  /// Cursor position in the construction plane (world coordinates); valid = false when the
  /// cursor left the view or the plane is seen edge-on.
  void cursorMoved(const QVector3D& world, bool valid);
  /// Emitted after each rendered frame (benchmark).
  void frameRendered();

 protected:
  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void releaseResources() override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool event(QEvent* event) override;

 private:
  enum class Drag { None, Pan, Orbit };

  void cameraChanged();
  void updateTitle();
  void updateTriadLabels();
  void showViewMenu();
  void reportCursor(const QPointF& px);

  ViewKind kind_;
  render::DisplayMode mode_ = render::DisplayMode::Shaded;
  Camera camera_;
  CPlane cplane_;

  Drag drag_ = Drag::None;
  QPointF last_pos_;

  const render::Scene* scene_ = nullptr;
  render::SceneRenderer scene_renderer_;
  render::LineRenderer lines_;
  render::LineLayer grid_layer_;
  render::LineLayer triad_layer_;
  bool grid_dirty_ = true;

  QLabel* title_ = nullptr;
  std::array<QLabel*, 3> triad_labels_{};
};

}  // namespace nfcad::viewport
