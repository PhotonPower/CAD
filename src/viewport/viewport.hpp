#pragma once

#include <QRhiWidget>

#include "render/display_mode.hpp"
#include "viewport/view_kind.hpp"

class QLabel;

namespace nfcad::viewport {

/// One 3D view (QRhiWidget: Metal / D3D / OpenGL·Vulkan, docs/rendering.md).
/// M0: clears to the background gradient colour and shows the view title.
class Viewport : public QRhiWidget {
  Q_OBJECT
 public:
  explicit Viewport(ViewKind kind, QWidget* parent = nullptr);

  [[nodiscard]] ViewKind kind() const { return kind_; }
  [[nodiscard]] render::DisplayMode displayMode() const { return mode_; }
  void setDisplayMode(render::DisplayMode mode);

 signals:
  void activated(nfcad::viewport::Viewport* view);
  void maximizeRequested(nfcad::viewport::Viewport* view);

 protected:
  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

 private:
  void updateTitle();

  ViewKind kind_;
  render::DisplayMode mode_ = render::DisplayMode::Shaded;
  QLabel* title_ = nullptr;
};

}  // namespace nfcad::viewport
