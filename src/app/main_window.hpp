#pragma once

#include <QMainWindow>

class QActionGroup;
class QDockWidget;
class QLabel;

namespace nfcad::ui {
class SnapBar;
class ToolBox;
}  // namespace nfcad::ui

namespace nfcad::viewport {
class Viewport;
class ViewportGrid;
}  // namespace nfcad::viewport

namespace nfcad::app {

/// Main window: tool column (left dock), 2x2 viewports (centre), view toolbar, command line,
/// status bar with snap toggles. Layout in docs/ui-concept.md.
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);

 protected:
  void showEvent(QShowEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private:
  void createMenus();
  void createViewToolBar();
  void createToolDock();
  void createCommandLine();
  void createStatusBar();
  void onActiveViewChanged(nfcad::viewport::Viewport* view);
  void onToolTriggered(const QString& tool_id);
  void showAbout();

  viewport::ViewportGrid* grid_ = nullptr;
  ui::ToolBox* toolbox_ = nullptr;
  ui::SnapBar* snapbar_ = nullptr;
  QDockWidget* tool_dock_ = nullptr;
  QActionGroup* display_modes_ = nullptr;
  QLabel* coords_ = nullptr;
  QLabel* active_view_ = nullptr;
  bool first_show_ = true;
};

}  // namespace nfcad::app
