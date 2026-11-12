#pragma once

#include <QWidget>
#include <functional>

class QLabel;
class QListWidget;
class QPushButton;

namespace nfcad::ui {

class NodeEditor;

/// The branches of the node workflow's model (RTT-Fusion R24): a list next to the node editor.
/// Selecting a branch highlights it; "New branch" records one by clicking the ports the light
/// leaves each block by (the modeller gives every branch – nothing is added automatically);
/// "Delete" removes the selected one. Shown only while the workflow is a model.
class BranchPanel : public QWidget {
  Q_OBJECT
 public:
  explicit BranchPanel(NodeEditor* editor, QWidget* parent = nullptr);
  /// Asks for the name of a recorded branch ("" cancels); a dialog unless replaced (tests).
  std::function<QString(const QString& suggestion)> ask_name;
  void refresh();
  [[nodiscard]] QListWidget* list() const { return list_; }

 private:
  NodeEditor* editor_;
  QLabel* hint_;
  QListWidget* list_;
  QPushButton* add_;
  QPushButton* remove_;
};

}  // namespace nfcad::ui
