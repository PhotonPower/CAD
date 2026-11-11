#pragma once

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

namespace nfcad::document {
class Document;
}
namespace nfcad::results {
class ResultStore;
}

namespace nfcad::ui {

/// Right dock "Results": result sets (one per run) and their items with visibility check boxes;
/// context menu: delete a run, convert rays to CAD curves (docs/plugins.md, P2 / D6).
class ResultsPanel : public QWidget {
  Q_OBJECT
 public:
  ResultsPanel(results::ResultStore* store, document::Document* doc, QWidget* parent = nullptr);

 private:
  void rebuild();
  void onItemChanged(QTreeWidgetItem* item, int column);
  void showContextMenu(const QPoint& pos);

  results::ResultStore* store_;
  document::Document* doc_;
  QTreeWidget* tree_ = nullptr;
  bool rebuilding_ = false;
};

/// Plot dock: the visible curve results (convergence of an optimisation, profiles, spectra),
/// updated live while points are appended.
class PlotWidget : public QWidget {
  Q_OBJECT
 public:
  explicit PlotWidget(const results::ResultStore* store, QWidget* parent = nullptr);
  [[nodiscard]] QSize sizeHint() const override { return {420, 220}; }

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  const results::ResultStore* store_;
};

}  // namespace nfcad::ui
