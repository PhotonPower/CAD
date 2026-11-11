#pragma once

#include <QMainWindow>

#include <QElapsedTimer>

class QActionGroup;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QToolButton;
class QMenu;

namespace nfcad::document {
class Document;
class Selection;
struct Entity;
}
namespace nfcad::render {
class Scene;
}
namespace nfcad::tessellation {
class TessellationService;
}
namespace nfcad::results {
class ResultStore;
}
namespace nfcad::plugins {
class PluginHost;
class PluginManager;
}
namespace nfcad::snap {
class SnapEngine;
struct SnapPoint;
}

namespace nfcad::ui {
class ImageView;
class ModelTreePanel;
class PluginPanel;
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
  ~MainWindow() override;

  /// Opens a .nfcad document or imports a STEP file (by extension). Returns false on errors
  /// (a message box has been shown unless `quiet`).
  bool openFile(const QString& path, bool quiet = false);
  /// After a crash in the last session: points to the local crash report (once).
  void reportPreviousCrash();
  /// Benchmark: after loading and tessellation, orbits the perspective view for `frames`
  /// frames, prints timings to stdout and quits the application.
  void runBenchmarkWhenReady(int frames);
  /// Sets the display mode of all views by name ("wireframe", "surfaces", "shaded").
  void setDisplayModeAll(const QString& name);

 protected:
  void showEvent(QShowEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private:
  // file handling (app/file_actions.cpp)
  void newDocument();
  void openDialog();
  void importStepDialog();
  void importStep(const QString& path, bool replace, bool quiet);
  bool save();
  bool saveAs();
  bool saveTo(const QString& path);
  void exportDialog(bool stl);
  /// Selected whole entities, or all displayed entities if nothing is selected.
  [[nodiscard]] std::vector<const document::Entity*> exportEntities() const;
  bool maybeSave();
  void updateTitle();
  void zoomExtentsWhenTessellated();
  void startBenchmark();

  // plugins (app/plugin_actions.cpp, docs/plugin-api.md)
  void createPlugins();
  void rebuildPluginUi();
  void runPluginAction(plugins::PluginHost* host, const QString& action);
  void installPlugin(const QString& package = {});
  void checkForUpdates();
  void connectPlugin(plugins::PluginHost* host);

  void createMenus();
  void createViewToolBar();
  void createToolDock();
  void createCommandLine();
  void createStatusBar();
  void onActiveViewChanged(nfcad::viewport::Viewport* view);
  void onToolTriggered(const QString& tool_id);
  void showAbout();
  void createPanels();
  void deleteSelection();
  void selectAll();
  void updateHoverInfo();


  document::Document* doc_ = nullptr;
  document::Selection* selection_ = nullptr;
  results::ResultStore* results_ = nullptr;
  plugins::PluginManager* plugins_ = nullptr;
  plugins::PluginHost* active_plugin_ = nullptr;
  bool plugin_started_on_empty_ = false;
  QMenu* plugins_menu_ = nullptr;
  QProgressBar* job_progress_ = nullptr;
  QToolButton* job_cancel_ = nullptr;
  QPlainTextEdit* plugin_log_ = nullptr;
  ui::PluginPanel* plugin_panel_ = nullptr;
  QLabel* developer_banner_ = nullptr;
  ui::ImageView* image_view_ = nullptr;
  std::uint64_t last_image_ = 0;
  snap::SnapEngine* snap_engine_ = nullptr;
  class CommandController* commands_ = nullptr;
  QLineEdit* command_line_ = nullptr;
  QToolButton* ortho_ = nullptr;
  ui::ModelTreePanel* model_tree_ = nullptr;
  tessellation::TessellationService* tessellator_ = nullptr;
  render::Scene* scene_ = nullptr;
  bool zoom_after_tessellation_ = false;
  QElapsedTimer load_timer_;
  int benchmark_frames_ = 0;

  QMenu* view_menu_ = nullptr;
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
