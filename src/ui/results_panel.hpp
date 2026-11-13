#pragma once

#include <QWidget>
#include <vector>

#include "results/result_types.hpp"

class QTreeWidget;
class QTreeWidgetItem;

namespace nfcad::document {
class Document;
}
namespace nfcad::results {
class ResultStore;
}

class QComboBox;
class QPainter;
class QTableWidget;

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
  /// Plots only the curves with this key (a display window); empty: all visible curves.
  void setKey(const QString& key) {
    key_ = key;
    update();
  }
  /// The curves plotted now.
  [[nodiscard]] std::vector<const results::ResultItem*> curves() const;
  /// The bar charts; the newest is drawn when there are no curves.
  [[nodiscard]] std::vector<const results::ResultItem*> bars() const;

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  void paintBars(QPainter& p, const results::ResultItem& item);
  const results::ResultStore* store_;
  QString key_;
};

/// Table dock: the visible table results (e.g. the orders of a grating), the newest shown – a
/// choice when there are several.
class TableView : public QWidget {
  Q_OBJECT
 public:
  explicit TableView(const results::ResultStore* store, QWidget* parent = nullptr);
  [[nodiscard]] QSize sizeHint() const override { return {420, 260}; }
  /// The visible tables.
  [[nodiscard]] std::vector<const results::ResultItem*> tables() const;
  /// The table shown now (nullptr if none).
  [[nodiscard]] const results::ResultItem* shown() const;
  [[nodiscard]] QTableWidget* grid() const { return table_; }

 private:
  void refresh();
  void show();

  const results::ResultStore* store_;
  QComboBox* choice_ = nullptr;
  QTableWidget* table_ = nullptr;
  results::ResultId newest_ = 0;
};

}  // namespace nfcad::ui
