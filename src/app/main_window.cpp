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
#include <cmath>

#include "document/document.hpp"
#include "document/selection.hpp"
#include "snap/snap_engine.hpp"
#include "ui/layer_panel.hpp"
#include "ui/property_panel.hpp"
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
  selection_ = new document::Selection(this);
  selection_->attach(doc_);
  snap_engine_ = new snap::SnapEngine;
  connect(doc_, &document::Document::documentReset, this, [this] { snap_engine_->clear(); });

  grid_ = new viewport::ViewportGrid(this);
  grid_->setScene(scene_);
  grid_->setSelection(selection_);
  grid_->setSnapEngine(snap_engine_);
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
  createPanels();
  connect(selection_, &document::Selection::hoverChanged, this, &MainWindow::updateHoverInfo);
  grid_->setSnapSettings(snapbar_->mask(), snapbar_->snappingEnabled());
  connect(snapbar_, &ui::SnapBar::maskChanged, this,
          [this](snap::SnapMask mask, bool enabled) { grid_->setSnapSettings(mask, enabled); });
  for (viewport::Viewport* v : grid_->views()) {
    connect(v, &viewport::Viewport::pointPicked, this,
            [this](const snap::SnapPoint& p, bool snapped, snap::SnapType type) {
              onPointPicked(p, snapped, static_cast<int>(type));
            });
    connect(v, &viewport::Viewport::pointInputFinished, this, [this] { finishCommand(); });
  }
  auto* enter = new QShortcut(QKeySequence(Qt::Key_Return), this);
  connect(enter, &QShortcut::activated, this, [this] { finishCommand(); });

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

MainWindow::~MainWindow() { delete snap_engine_; }

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
  edit->addSeparator();
  QAction* del = edit->addAction(tr("&Delete"), this, &MainWindow::deleteSelection);
  del->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
  edit->addAction(tr("Select &All"), QKeySequence::SelectAll, this, &MainWindow::selectAll);
  QAction* none = edit->addAction(tr("Select &None / Cancel"), this, [this] {
    if (command_ != Command::None) {
      finishCommand(tr("Cancelled"));
    } else {
      selection_->clear();
    }
  });
  none->setShortcut(QKeySequence(Qt::Key_Escape));

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
  command_line_ = line;
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
    if (auto* props = findChild<QDockWidget*>(QStringLiteral("propertyDock"))) {
      resizeDocks({props}, {260}, Qt::Horizontal);
    }
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

void MainWindow::setPrompt(const QString& prompt) {
  command_line_->setPlaceholderText(prompt.isEmpty() ? tr("Command:") : prompt);
}

void MainWindow::startCommand(Command command) {
  if (command_ != Command::None) finishCommand();
  command_ = command;
  first_point_.clear();
  grid_->setPointInput(true);
  if (grid_->activeView() != nullptr) grid_->activeView()->setFocus();
  setPrompt(command == Command::Point
                ? tr("Point: click locations (snaps: F3, Tab cycles, Alt suppresses) – "
                     "Enter / right-click / Esc to finish")
                : tr("Distance: first point"));
}

void MainWindow::finishCommand(const QString& message) {
  if (command_ == Command::None) return;
  command_ = Command::None;
  first_point_.clear();
  grid_->setPointInput(false);
  setPrompt({});
  if (!message.isEmpty()) statusBar()->showMessage(message, 6000);
}

void MainWindow::onPointPicked(const snap::SnapPoint& p, bool snapped, int type) {
  const QString how = snapped ? tr(" (%1)").arg(QString::fromLatin1(
                                    snap::short_name(static_cast<snap::SnapType>(type))))
                              : QString();
  const QString coords = QStringLiteral("%1, %2, %3")
                             .arg(p.x, 0, 'f', 4)
                             .arg(p.y, 0, 'f', 4)
                             .arg(p.z, 0, 'f', 4);
  switch (command_) {
    case Command::Point:
      doc_->addEntities({document::make_point_entity(p.x, p.y, p.z)}, tr("Point"));
      statusBar()->showMessage(tr("Point at %1%2").arg(coords, how), 6000);
      break;
    case Command::Distance:
      if (first_point_.empty()) {
        first_point_ = {p.x, p.y, p.z};
        setPrompt(tr("Distance: second point (first: %1%2)").arg(coords, how));
      } else {
        const double dx = p.x - first_point_[0];
        const double dy = p.y - first_point_[1];
        const double dz = p.z - first_point_[2];
        const QString msg = tr("Distance %1   (dx %2, dy %3, dz %4)")
                                .arg(std::sqrt(dx * dx + dy * dy + dz * dz), 0, 'g', 10)
                                .arg(dx, 0, 'g', 8)
                                .arg(dy, 0, 'g', 8)
                                .arg(dz, 0, 'g', 8);
        finishCommand();
        statusBar()->showMessage(msg);
        command_line_->setPlaceholderText(msg);
      }
      break;
    case Command::None: break;
  }
}

void MainWindow::createPanels() {
  auto* props = new QDockWidget(tr("Properties"), this);
  props->setObjectName(QStringLiteral("propertyDock"));
  props->setWidget(new ui::PropertyPanel(doc_, selection_, props));
  props->setMinimumWidth(220);
  addDockWidget(Qt::RightDockWidgetArea, props);
  auto* layers = new QDockWidget(tr("Layers"), this);
  layers->setObjectName(QStringLiteral("layerDock"));
  layers->setWidget(new ui::LayerPanel(doc_, selection_, layers));
  addDockWidget(Qt::RightDockWidgetArea, layers);
  splitDockWidget(props, layers, Qt::Vertical);
  view_menu_->addAction(props->toggleViewAction());
  view_menu_->addAction(layers->toggleViewAction());
}

void MainWindow::deleteSelection() {
  const std::vector<document::EntityId> ids = [this] {
    std::vector<document::EntityId> out;
    for (const document::SelectionItem& i : selection_->items()) {
      if (i.sub.kind == document::SubElement::Kind::None) out.push_back(i.entity);
    }
    return out;
  }();
  if (ids.empty()) {
    if (!selection_->empty()) {
      statusBar()->showMessage(tr("Faces, edges and vertices cannot be deleted on their own"), 4000);
    }
    return;
  }
  doc_->removeEntities(ids, tr("Delete %n object(s)", nullptr, static_cast<int>(ids.size())));
}

void MainWindow::selectAll() {
  std::vector<document::SelectionItem> items;
  for (const auto& [id, e] : doc_->entities()) {
    if (doc_->isDisplayed(e)) items.push_back({id, {}});
  }
  selection_->apply(items, document::Selection::Mode::Replace);
}

void MainWindow::updateHoverInfo() {
  const auto h = selection_->hovered();
  if (!h) {
    statusBar()->clearMessage();
    return;
  }
  const document::Entity* e = doc_->entity(h->entity);
  if (e == nullptr) return;
  QString text = (e->name.isEmpty() ? tr("Entity %1").arg(e->id) : e->name) +
                 QStringLiteral(" · ") + document::kind_name(document::kind_of(*e->geometry));
  if (h->sub.kind != document::SubElement::Kind::None) {
    text += QStringLiteral(" · ") + document::sub_element_name(h->sub);
  }
  statusBar()->showMessage(text);
}

void MainWindow::onToolTriggered(const QString& tool_id) {
  if (tool_id == QLatin1String("delete")) {
    deleteSelection();
    return;
  }
  if (tool_id == QLatin1String("point")) {
    startCommand(Command::Point);
    return;
  }
  if (tool_id == QLatin1String("distance")) {
    startCommand(Command::Distance);
    return;
  }
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
