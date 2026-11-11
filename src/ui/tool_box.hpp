#pragma once

#include <QScrollArea>
#include <QWidget>
#include <vector>

#include "ui/tool_catalog.hpp"

class QToolButton;

namespace nfcad::ui {

/// One collapsible group: header row (arrow + title) and a grid of icon buttons whose column
/// count follows the available width.
class ToolGroup : public QWidget {
  Q_OBJECT
 public:
  explicit ToolGroup(const ToolGroupDef& def, QWidget* parent = nullptr);

  [[nodiscard]] QString groupId() const { return id_; }
  [[nodiscard]] bool isExpanded() const;
  void setExpanded(bool expanded);

 signals:
  void toolTriggered(const QString& toolId);
  void expandedChanged(bool expanded);

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void relayout();

  QString id_;
  QToolButton* header_ = nullptr;
  QWidget* body_ = nullptr;
  std::vector<QToolButton*> buttons_;
  int columns_ = 0;
};

/// The tool column on the left: all groups of the tool catalog in a vertical scroll area.
/// The expanded state of each group is stored in QSettings.
class ToolBox : public QScrollArea {
  Q_OBJECT
 public:
  explicit ToolBox(QWidget* parent = nullptr);

 signals:
  void toolTriggered(const QString& toolId);
};

}  // namespace nfcad::ui
