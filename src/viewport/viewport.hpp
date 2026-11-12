#pragma once

#include <QPointF>
#include <QRhiWidget>
#include <array>

#include "render/display_mode.hpp"
#include "render/line_renderer.hpp"
#include "render/result_renderer.hpp"
#include "render/scene_renderer.hpp"
#include "snap/snap_engine.hpp"
#include "viewport/camera.hpp"
#include "viewport/grid.hpp"
#include "viewport/view_kind.hpp"

class QLabel;

namespace nfcad::document {
class Selection;
}

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
  /// Results to show (rays, fields on faces); may be null.
  void setResults(const results::ResultStore* results);

  /// Selection to show and to change by clicking / dragging; may be null.
  void setSelection(document::Selection* selection);

  // ---- point input with object snaps (M4, docs/snapping.md)
  void setSnapEngine(snap::SnapEngine* engine) { snap_engine_ = engine; }
  void setSnapSettings(snap::SnapMask mask, bool enabled);
  /// While on, the cursor snaps (magnet radius 10 px, Tab cycles candidates, Alt suppresses)
  /// and a left click emits pointPicked; a right click emits pointInputFinished.
  void setPointInput(bool on);
  [[nodiscard]] bool pointInput() const { return point_input_; }
  /// Rubber-band preview of the active command (world polylines), drawn in all views.
  void setPreview(const std::vector<std::vector<QVector3D>>& polylines);
  /// Current snap candidates under the cursor (best first) and the chosen one.
  [[nodiscard]] const std::vector<snap::SnapResult>& snapCandidates() const { return snaps_; }

  /// Orbits like a mouse drag by `delta_px` (benchmark, scripted views).
  void orbitBy(const QPointF& delta_px);

  /// Shows all visible geometry (a default region around the origin if there is none).
  void zoomExtents();
  /// Fits the selected entities and sub-elements into the view (zoom extents without selection).
  void zoomSelected();

 signals:
  void activated(nfcad::viewport::Viewport* view);
  void maximizeRequested(nfcad::viewport::Viewport* view);
  /// Cursor position in the construction plane (world coordinates); valid = false when the
  /// cursor left the view or the plane is seen edge-on.
  void cursorMoved(const QVector3D& world, bool valid);
  /// Emitted after each rendered frame (benchmark).
  void frameRendered();
  /// Point input: a point was clicked; `type` is valid if `snapped`.
  void pointPicked(const nfcad::snap::SnapPoint& point, bool snapped, nfcad::snap::SnapType type);
  void pointInputFinished();
  /// Point input: the (snapped) point under the cursor changed.
  void pointerMoved(const nfcad::snap::SnapPoint& point, bool valid, bool snapped);
  /// Printable text typed while the view has the focus (goes to the command line).
  void textTyped(const QString& text);
  /// Enter / Return pressed in the view.
  void enterPressed();

 protected:
  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void releaseResources() override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool event(QEvent* event) override;

 private:
  enum class Drag { None, Pan, Orbit };
  enum class LeftAction { None, Pending, Rectangle };

  void hoverPick(const QPointF& px, Qt::KeyboardModifiers mods);
  void clickSelect(const QPointF& px, Qt::KeyboardModifiers mods);
  void rectSelect(const QRectF& rect, bool crossing, Qt::KeyboardModifiers mods);
  void updateSnap(const QPointF& px, Qt::KeyboardModifiers mods);
  void showSnap();

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
  document::Selection* selection_ = nullptr;
  snap::SnapEngine* snap_engine_ = nullptr;
  snap::SnapMask snap_mask_ = snap::SnapMask::defaults();
  bool snap_enabled_ = true;
  bool point_input_ = false;
  std::vector<snap::SnapResult> snaps_;
  std::size_t snap_index_ = 0;
  QPointF snap_px_{-1, -1};
  snap::SnapPoint cursor_point_;  // snapped or construction-plane point under the cursor
  bool cursor_point_valid_ = false;
  bool drag_moved_ = false;
  class SnapMarker* marker_ = nullptr;
  LeftAction left_ = LeftAction::None;
  QPointF press_pos_;
  class SelectionRect* rubber_ = nullptr;
  render::SceneRenderer scene_renderer_;
  render::ResultRenderer result_renderer_;
  const results::ResultStore* results_ = nullptr;
  class FieldLegend* legend_ = nullptr;
  void updateLegend();
  render::LineRenderer lines_;
  render::LineLayer grid_layer_;
  render::LineLayer triad_layer_;
  render::LineLayer preview_layer_;
  bool grid_dirty_ = true;

  QLabel* title_ = nullptr;
  std::array<QLabel*, 3> triad_labels_{};
};

}  // namespace nfcad::viewport
