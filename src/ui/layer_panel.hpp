#pragma once

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

namespace nfcad::document {
class Document;
class Selection;
}  // namespace nfcad::document

namespace nfcad::ui {

/// Right dock: layers with visibility, colour (double-click the swatch) and name (editable);
/// buttons to add a layer and to move the selection to the current layer.
class LayerPanel : public QWidget {
  Q_OBJECT
 public:
  LayerPanel(document::Document* doc, document::Selection* selection, QWidget* parent = nullptr);

 private:
  void rebuild();
  void onItemChanged(QTreeWidgetItem* item, int column);
  void onItemDoubleClicked(QTreeWidgetItem* item, int column);

  document::Document* doc_;
  document::Selection* selection_;
  QTreeWidget* tree_ = nullptr;
  bool rebuilding_ = false;
};

}  // namespace nfcad::ui
