#pragma once

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

namespace nfcad::document {
class Document;
class Selection;
}  // namespace nfcad::document

namespace nfcad::ui {

/// Right dock "Model": groups and objects as a tree (docs/plugins.md, P1). Check boxes switch the
/// visibility, selecting items selects the objects in the views (and vice versa); the context
/// menu groups, ungroups, isolates and shows everything again. Group names are editable.
class ModelTreePanel : public QWidget {
  Q_OBJECT
 public:
  ModelTreePanel(document::Document* doc, document::Selection* selection, QWidget* parent = nullptr);

  void groupSelection();
  void ungroupSelection();
  void hideSelection();
  void isolateSelection();

 private:
  void scheduleRebuild();
  void rebuild();
  void syncFromSelection();
  void onItemChanged(QTreeWidgetItem* item, int column);
  void onTreeSelection();
  void showContextMenu(const QPoint& pos);
  [[nodiscard]] std::vector<std::uint64_t> selectedEntities() const;

  document::Document* doc_;
  document::Selection* selection_;
  QTreeWidget* tree_ = nullptr;
  bool rebuilding_ = false;
  bool rebuild_pending_ = false;
  bool syncing_ = false;
};

}  // namespace nfcad::ui
