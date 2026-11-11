#include "app/main_window.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QToolBar>
#include <algorithm>

#include "kernel/kernel_info.hpp"
#include "ui/snap_bar.hpp"
#include "ui/tool_box.hpp"
#include "viewport/viewport.hpp"
#include "viewport/viewport_grid.hpp"

namespace nfcad::app {

using render::DisplayMode;

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(tr("Nurbforge CAD"));
  // No setUnifiedTitleAndToolBarOnMac(true): with it QRhiWidget gets no QRhi (Qt 6.12, macOS).

  grid_ = new viewport::ViewportGrid(this);
  setCentralWidget(grid_);

  createMenus();
  createViewToolBar();
  createToolDock();
  createCommandLine();
  createStatusBar();

  connect(grid_, &viewport::ViewportGrid::activeViewChanged, this,
          &MainWindow::onActiveViewChanged);
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

void MainWindow::createMenus() {
  QMenu* file = menuBar()->addMenu(tr("&File"));
  QAction* open = file->addAction(tr("&Open…"));
  open->setShortcut(QKeySequence::Open);
  open->setEnabled(false);  // M2: STEP import, .nfcad documents
  file->addSeparator();
  QAction* quit = file->addAction(tr("&Quit"), this, &QWidget::close);
  quit->setShortcut(QKeySequence::Quit);
  quit->setMenuRole(QAction::QuitRole);

  menuBar()->addMenu(tr("&View"));  // filled in createViewToolBar / createToolDock

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
  QMenu* view_menu = menuBar()->actions().at(1)->menu();
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
  zoom_extents->setEnabled(false);  // M1
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
  menuBar()->actions().at(1)->menu()->addAction(tool_dock_->toggleViewAction());
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
  QSettings settings;
  settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
  settings.setValue(QStringLiteral("window/state"), saveState());
  QMainWindow::closeEvent(event);
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
