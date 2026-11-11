#include "app/main_window.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <algorithm>

#include "document/document.hpp"
#include "kernel/kernel_info.hpp"
#include "render/scene.hpp"
#include "tessellation/tessellation_service.hpp"
#include "ui/snap_bar.hpp"
#include "ui/tool_box.hpp"
#include "viewport/viewport.hpp"
#include "viewport/viewport_grid.hpp"

namespace nfcad::app {

using render::DisplayMode;

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  // No setUnifiedTitleAndToolBarOnMac(true): with it QRhiWidget gets no QRhi (Qt 6.12, macOS).
  doc_ = new document::Document(this);
  tessellator_ = new tessellation::TessellationService(this);
  scene_ = new render::Scene(doc_, tessellator_, this);

  grid_ = new viewport::ViewportGrid(this);
  grid_->setScene(scene_);
  setCentralWidget(grid_);

  connect(doc_->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);
  connect(tessellator_, &tessellation::TessellationService::idle, this, [this] {
    if (zoom_after_tessellation_) {
      zoom_after_tessellation_ = false;
      grid_->zoomExtentsAll();
      statusBar()->showMessage(tr("%1 entities, %2 triangles – %3 s")
                                   .arg(doc_->entities().size())
                                   .arg(scene_->triangleCount())
                                   .arg(load_timer_.elapsed() / 1000.0, 0, 'f', 2),
                               8000);
      if (benchmark_frames_ > 0) QTimer::singleShot(500, this, &MainWindow::startBenchmark);
    }
  });
  updateTitle();

  createMenus();
  createViewToolBar();
  createToolDock();
  createCommandLine();
  createStatusBar();

  connect(grid_, &viewport::ViewportGrid::activeViewChanged, this,
          &MainWindow::onActiveViewChanged);
  for (viewport::Viewport* v : grid_->views()) {
    connect(v, &viewport::Viewport::cursorMoved, this, [this](const QVector3D& p, bool valid) {
      coords_->setText(valid ? QStringLiteral("X %1   Y %2   Z %3")
                                   .arg(p.x(), 0, 'f', 3)
                                   .arg(p.y(), 0, 'f', 3)
                                   .arg(p.z(), 0, 'f', 3)
                             : QStringLiteral("X —  Y —  Z —"));
    });
  }
  auto* maximize = new QShortcut(QKeySequence(Qt::Key_Space), this);
  connect(maximize, &QShortcut::activated, this,
          [this] { grid_->toggleMaximized(grid_->activeView()); });

  onActiveViewChanged(grid_->activeView());

  QSettings settings;
  if (!restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray())) {
    resize(1440, 900);
  } else {
    first_show_ = false;  // keep the user's dock width
  }
  restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
}

MainWindow::~MainWindow() = default;

void MainWindow::createMenus() {
  QMenu* file = menuBar()->addMenu(tr("&File"));
  file->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newDocument);
  file->addAction(tr("&Open…"), QKeySequence::Open, this, &MainWindow::openDialog);
  file->addAction(tr("&Import STEP…"), QKeySequence(Qt::CTRL | Qt::Key_I), this,
                  &MainWindow::importStepDialog);
  file->addSeparator();
  file->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
  file->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, &MainWindow::saveAs);
  file->addSeparator();
  QAction* quit = file->addAction(tr("&Quit"), this, &QWidget::close);
  quit->setShortcut(QKeySequence::Quit);
  quit->setMenuRole(QAction::QuitRole);

  QMenu* edit = menuBar()->addMenu(tr("&Edit"));
  QAction* undo = doc_->undoStack()->createUndoAction(this, tr("&Undo"));
  undo->setShortcut(QKeySequence::Undo);
  QAction* redo = doc_->undoStack()->createRedoAction(this, tr("&Redo"));
  redo->setShortcut(QKeySequence::Redo);
  edit->addAction(undo);
  edit->addAction(redo);

  view_menu_ = menuBar()->addMenu(tr("&View"));  // filled in createViewToolBar / createToolDock

  QMenu* help = menuBar()->addMenu(tr("&Help"));
  QAction* about = help->addAction(tr("About Nurbforge CAD"), this, &MainWindow::showAbout);
  about->setMenuRole(QAction::AboutRole);
}

void MainWindow::createViewToolBar() {
  QToolBar* bar = addToolBar(tr("View"));
  bar->setObjectName(QStringLiteral("viewToolBar"));
  bar->setMovable(false);

  display_modes_ = new QActionGroup(this);
  display_modes_->setExclusive(true);
  struct Mode {
    DisplayMode mode;
    QString text;
    QString tip;
  };
  const Mode modes[] = {
      {DisplayMode::Wireframe, tr("Wireframe"), tr("Wireframe: curves, edges and iso-curves")},
      {DisplayMode::Surfaces, tr("Surfaces"), tr("Surfaces: flat faces with edges")},
      {DisplayMode::Shaded, tr("Shaded"), tr("Shaded: lit faces with edges")},
  };
  QMenu* view_menu = view_menu_;
  for (const Mode& m : modes) {
    QAction* action = bar->addAction(m.text);
    action->setCheckable(true);
    action->setData(static_cast<int>(m.mode));
    action->setToolTip(m.tip + tr("\nShift-click: all views"));
    display_modes_->addAction(action);
    view_menu->addAction(action);
    connect(action, &QAction::triggered, this, [this, mode = m.mode] {
      const bool all = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
      grid_->setDisplayMode(mode, all);
    });
  }
  bar->addSeparator();
  QAction* zoom_extents = bar->addAction(tr("Zoom extents"));
  zoom_extents->setShortcut(QKeySequence(Qt::Key_Z, Qt::Key_E));
  zoom_extents->setToolTip(tr("Zoom extents (Z, E)\nShift-click: all views"));
  view_menu->addAction(zoom_extents);
  connect(zoom_extents, &QAction::triggered, this, [this] {
    const bool all = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
    for (viewport::Viewport* v : grid_->views()) {
      if (all || v == grid_->activeView()) v->zoomExtents();
    }
  });
  view_menu->addSeparator();
}

void MainWindow::createToolDock() {
  toolbox_ = new ui::ToolBox(this);
  tool_dock_ = new QDockWidget(tr("Tools"), this);
  tool_dock_->setObjectName(QStringLiteral("toolDock"));
  tool_dock_->setWidget(toolbox_);
  tool_dock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
  tool_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  tool_dock_->setMinimumWidth(96);
  addDockWidget(Qt::LeftDockWidgetArea, tool_dock_);
  view_menu_->addAction(tool_dock_->toggleViewAction());
  connect(toolbox_, &ui::ToolBox::toolTriggered, this, &MainWindow::onToolTriggered);
}

void MainWindow::createCommandLine() {
  auto* line = new QLineEdit(this);
  line->setObjectName(QStringLiteral("commandLine"));
  line->setPlaceholderText(tr("Command:"));
  auto* dock = new QDockWidget(tr("Command line"), this);
  dock->setObjectName(QStringLiteral("commandDock"));
  dock->setWidget(line);
  dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
  dock->setTitleBarWidget(new QWidget(dock));  // no title bar
  addDockWidget(Qt::BottomDockWidgetArea, dock);
}

void MainWindow::createStatusBar() {
  coords_ = new QLabel(QStringLiteral("X —  Y —  Z —"), this);
  coords_->setMinimumWidth(220);
  active_view_ = new QLabel(this);
  snapbar_ = new ui::SnapBar(this);
  statusBar()->addWidget(coords_);
  statusBar()->addWidget(active_view_);
  statusBar()->addPermanentWidget(snapbar_);
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  if (first_show_) {
    first_show_ = false;
    // Tool column ~10 % of the window width (docs/ui-concept.md).
    const int width = std::clamp(this->width() / 10, 96, 280);
    resizeDocks({tool_dock_}, {width}, Qt::Horizontal);
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (!maybeSave()) {
    event->ignore();
    return;
  }
  QSettings settings;
  settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
  settings.setValue(QStringLiteral("window/state"), saveState());
  QMainWindow::closeEvent(event);
}

void MainWindow::setDisplayModeAll(const QString& name) {
  const QString n = name.toLower();
  const DisplayMode mode = n == QLatin1String("wireframe") ? DisplayMode::Wireframe
                           : n == QLatin1String("surfaces") ? DisplayMode::Surfaces
                                                            : DisplayMode::Shaded;
  grid_->setDisplayMode(mode, true);
  onActiveViewChanged(grid_->activeView());
}

void MainWindow::onActiveViewChanged(viewport::Viewport* view) {
  if (view == nullptr) return;
  active_view_->setText(tr("Active view: %1").arg(viewport::view_title(view->kind())));
  for (QAction* action : display_modes_->actions()) {
    action->setChecked(action->data().toInt() == static_cast<int>(view->displayMode()));
  }
}

void MainWindow::onToolTriggered(const QString& tool_id) {
  statusBar()->showMessage(tr("Tool '%1' is not implemented yet (see docs/plan.md)").arg(tool_id),
                           4000);
}

void MainWindow::showAbout() {
  const bool kernel_ok = kernel::self_test();
  QMessageBox::about(
      this, tr("About Nurbforge CAD"),
      tr("<b>Nurbforge CAD</b> %1<br>Desktop CAD front end for the Nurbforge NURBS kernel.<br><br>"
         "Nurbforge kernel %2 – self-test %3<br>Qt %4")
          .arg(QStringLiteral(NFCAD_VERSION),
               QString::fromStdString(kernel::nurbforge_version()),
               kernel_ok ? tr("passed") : tr("FAILED"), QString::fromLatin1(qVersion())));
}

}  // namespace nfcad::app
