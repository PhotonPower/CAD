#pragma once

#include <QWidget>
#include <array>

#include "render/display_mode.hpp"
#include "snap/snap_types.hpp"

namespace nfcad::render {
class Scene;
}
namespace nfcad::document {
class Selection;
}
namespace nfcad::snap {
class SnapEngine;
}

class QFrame;
class QSplitter;

namespace nfcad::viewport {

class Viewport;

/// 2x2 grid: Front | Top over Left | Perspective. Splitters of both rows move together.
/// Tracks the active view; maximise/restore one view (double-click title, Space).
class ViewportGrid : public QWidget {
  Q_OBJECT
 public:
  explicit ViewportGrid(QWidget* parent = nullptr);

  [[nodiscard]] Viewport* activeView() const { return active_; }
  [[nodiscard]] const std::array<Viewport*, 4>& views() const { return views_; }

  void setScene(const render::Scene* scene);
  void setSelection(document::Selection* selection);
  void setSnapEngine(snap::SnapEngine* engine);
  void setSnapSettings(snap::SnapMask mask, bool enabled);
  void setPointInput(bool on);
  void zoomExtentsAll();

  /// Sets the display mode of the active view, or of all views if `all` is true.
  void setDisplayMode(render::DisplayMode mode, bool all);
  void toggleMaximized(Viewport* view);

 signals:
  void activeViewChanged(nfcad::viewport::Viewport* view);

 private:
  void setActive(Viewport* view);

  std::array<Viewport*, 4> views_{};
  std::array<QFrame*, 4> frames_{};
  QSplitter* rows_ = nullptr;
  std::array<QSplitter*, 2> columns_{};
  Viewport* active_ = nullptr;
  Viewport* maximized_ = nullptr;
};

}  // namespace nfcad::viewport
