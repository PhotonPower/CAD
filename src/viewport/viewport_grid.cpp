#include "viewport/viewport_grid.hpp"

#include <QFrame>
#include <QSplitter>
#include <QStyle>
#include <QVBoxLayout>

#include "viewport/viewport.hpp"

namespace nfcad::viewport {

ViewportGrid::ViewportGrid(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  rows_ = new QSplitter(Qt::Vertical, this);
  rows_->setChildrenCollapsible(false);
  layout->addWidget(rows_);

  constexpr std::array<ViewKind, 4> kinds = {ViewKind::Front, ViewKind::Top, ViewKind::Left,
                                             ViewKind::Perspective};
  for (std::size_t r = 0; r < 2; ++r) {
    columns_[r] = new QSplitter(Qt::Horizontal, rows_);
    columns_[r]->setChildrenCollapsible(false);
    rows_->addWidget(columns_[r]);
    for (std::size_t c = 0; c < 2; ++c) {
      const std::size_t i = 2 * r + c;
      auto* frame = new QFrame(columns_[r]);
      frame->setObjectName(QStringLiteral("viewportFrame"));
      auto* frame_layout = new QVBoxLayout(frame);
      frame_layout->setContentsMargins(2, 2, 2, 2);
      auto* view = new Viewport(kinds[i], frame);
      frame_layout->addWidget(view);
      columns_[r]->addWidget(frame);
      views_[i] = view;
      frames_[i] = frame;
      connect(view, &Viewport::activated, this, &ViewportGrid::setActive);
      connect(view, &Viewport::maximizeRequested, this, &ViewportGrid::toggleMaximized);
    }
  }

  // Keep the vertical divider aligned in both rows (classic 4-view CAD layout).
  connect(columns_[0], &QSplitter::splitterMoved, this,
          [this] { columns_[1]->setSizes(columns_[0]->sizes()); });
  connect(columns_[1], &QSplitter::splitterMoved, this,
          [this] { columns_[0]->setSizes(columns_[1]->sizes()); });

  setActive(views_[3]);
}

void ViewportGrid::setActive(Viewport* view) {
  if (view == active_) return;
  active_ = view;
  for (std::size_t i = 0; i < views_.size(); ++i) {
    frames_[i]->setProperty("active", views_[i] == view);
    frames_[i]->style()->unpolish(frames_[i]);
    frames_[i]->style()->polish(frames_[i]);
  }
  emit activeViewChanged(view);
}

void ViewportGrid::setScene(const render::Scene* scene) {
  for (Viewport* v : views_) v->setScene(scene);
}

void ViewportGrid::setSelection(document::Selection* selection) {
  for (Viewport* v : views_) v->setSelection(selection);
}

void ViewportGrid::zoomExtentsAll() {
  for (Viewport* v : views_) v->zoomExtents();
}

void ViewportGrid::setDisplayMode(render::DisplayMode mode, bool all) {
  for (Viewport* v : views_) {
    if (all || v == active_) v->setDisplayMode(mode);
  }
}

void ViewportGrid::toggleMaximized(Viewport* view) {
  if (view == nullptr) return;
  maximized_ = (maximized_ == view) ? nullptr : view;
  for (std::size_t i = 0; i < views_.size(); ++i) {
    frames_[i]->setVisible(maximized_ == nullptr || views_[i] == maximized_);
  }
  for (std::size_t r = 0; r < 2; ++r) {
    const bool row_visible =
        maximized_ == nullptr || views_[2 * r] == maximized_ || views_[2 * r + 1] == maximized_;
    columns_[r]->setVisible(row_visible);
  }
  setActive(view);
}

}  // namespace nfcad::viewport
