#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace nfcad::document {
class Document;
class Selection;
}  // namespace nfcad::document

namespace nfcad::ui {

/// Right dock: properties of the selection (docs/ui-concept.md). One item: name, layer and the
/// geometric data from kernel::describe; several items: counts per type.
class PropertyPanel : public QWidget {
  Q_OBJECT
 public:
  PropertyPanel(document::Document* doc, document::Selection* selection, QWidget* parent = nullptr);

 private:
  void refresh();
  void onItemChanged(QTreeWidgetItem* item, int column);
  void addAttribute();

  document::Document* doc_;
  document::Selection* selection_;
  QLabel* header_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QPushButton* add_attribute_ = nullptr;
  bool refreshing_ = false;
};

}  // namespace nfcad::ui
