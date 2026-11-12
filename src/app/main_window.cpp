#include "app/main_window.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <algorithm>
#include <cmath>

#include "document/document.hpp"
#include "document/selection.hpp"
#include "app/command_controller.hpp"
#include "app/demo_results.hpp"
#include "plugins/plugin_manager.hpp"
#include "results/result_store.hpp"
#include "ui/results_panel.hpp"
#include "snap/snap_engine.hpp"
#include "support/crash_handler.hpp"
#include "ui/image_view.hpp"
#include "ui/branch_panel.hpp"
#include "ui/node_editor.hpp"
#include "ui/layer_panel.hpp"
#include "ui/model_tree_panel.hpp"
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

namespace {
constexpr int kWindowStateVersion = 7;  // 2: model tree (P1), 3: results + plot (P2), 4: plugin log (P3), 5: plugin panel (P4), 6: image dock, 7: node workflow
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  // No setUnifiedTitleAndToolBarOnMac(true): with it QRhiWidget gets no QRhi (Qt 6.12, macOS).
  doc_ = new document::Document(this);
  tessellator_ = new tessellation::TessellationService(this);
  scene_ = new render::Scene(doc_, tessellator_, this);
  selection_ = new document::Selection(this);
  selection_->attach(doc_);
  results_ = new results::ResultStore(this);
  connect(doc_, &document::Document::documentReset, results_, &results::ResultStore::clear);
  snap_engine_ = new snap::SnapEngine;
  connect(doc_, &document::Document::documentReset, this, [this] { snap_engine_->clear(); });

  grid_ = new viewport::ViewportGrid(this);
  grid_->setScene(scene_);
  grid_->setSelection(selection_);
  grid_->setResults(results_);
  grid_->setSnapEngine(snap_engine_);
  setCentralWidget(grid_);

  connect(doc_->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);
  connect(doc_, &document::Document::modificationChanged, this, &MainWindow::updateTitle);
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
  createPlugins();
  grid_->setSnapSettings(snapbar_->mask(), snapbar_->snappingEnabled());
  connect(snapbar_, &ui::SnapBar::maskChanged, this,
          [this](snap::SnapMask mask, bool enabled) { grid_->setSnapSettings(mask, enabled); });
  commands_ = new CommandController(doc_, selection_, grid_, command_line_, statusBar(), this);
  connect(ortho_, &QToolButton::toggled, commands_, &CommandController::setOrtho);
  connect(commands_, &CommandController::orthoChanged, ortho_, &QToolButton::setChecked);

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
  // the version changes whenever docks are added, so that an old layout does not hide them
  restoreState(settings.value(QStringLiteral("window/state")).toByteArray(), kWindowStateVersion);
}

MainWindow::~MainWindow() {
  // Stopping the plugin processes emits signals; the window must not receive them while it is
  // being destroyed.
  if (plugins_ != nullptr) {
    for (const auto& h : plugins_->plugins()) h->disconnect(this);
    plugins_->disconnect(this);
    delete plugins_;
    plugins_ = nullptr;
  }
  // The document is destroyed after this body; clearing its undo stack emits cleanChanged, and
  // other document signals may follow – none of them may reach this half-destroyed window.
  if (doc_ != nullptr) {
    doc_->undoStack()->disconnect(this);
    doc_->disconnect(this);
  }
  delete snap_engine_;
}

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
  file->addAction(tr("Export S&TEP…"), this, [this] { exportDialog(false); });
  file->addAction(tr("Export ST&L…"), this, [this] { exportDialog(true); });
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
  edit->addSeparator();
  edit->addAction(tr("&Group…"), QKeySequence(Qt::CTRL | Qt::Key_G), this,
                  [this] { model_tree_->groupSelection(); });
  edit->addAction(tr("U&ngroup"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), this,
                  [this] { model_tree_->ungroupSelection(); });
  edit->addAction(tr("&Hide selected"), this, [this] { model_tree_->hideSelection(); });
  edit->addAction(tr("&Isolate selected"), this, [this] { model_tree_->isolateSelection(); });
  edit->addAction(tr("Show &all"), this, [this] { doc_->showAll(); });
  edit->addSeparator();
  QAction* none = edit->addAction(tr("Select &None / Cancel"), this, [this] {
    if (commands_->active()) {
      commands_->cancel();
    } else if (active_plugin_ != nullptr && active_plugin_->activeJob()) {
      active_plugin_->cancel();
    } else {
      selection_->clear();
    }
  });
  none->setShortcut(QKeySequence(Qt::Key_Escape));

  view_menu_ = menuBar()->addMenu(tr("&View"));  // filled in createViewToolBar / createToolDock

  QMenu* help = menuBar()->addMenu(tr("&Help"));
  help->addAction(tr("Demo results on the selected object"), this, [this] {
    const auto ids = selection_->entities();
    const QString error = make_demo_results(*results_, *scene_, ids.empty() ? 0 : ids.front());
    if (!error.isEmpty()) statusBar()->showMessage(error, 5000);
  });
  help->addAction(tr("Check for updates…"), this, &MainWindow::checkForUpdates);
  QAction* about = help->addAction(tr("About Raytatouille Fusion"), this, &MainWindow::showAbout);
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
  QAction* zoom_selected = bar->addAction(tr("Zoom selected"));
  zoom_selected->setShortcut(QKeySequence(Qt::Key_Z, Qt::Key_S));
  zoom_selected->setToolTip(tr("Zoom selected (Z, S): the selected elements fill the view\nShift-click: all views"));
  view_menu->addAction(zoom_selected);
  connect(zoom_selected, &QAction::triggered, this, [this] {
    const bool all = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
    for (viewport::Viewport* v : grid_->views()) {
      if (all || v == grid_->activeView()) v->zoomSelected();
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
  ortho_ = new QToolButton(this);
  ortho_->setText(tr("Ortho"));
  ortho_->setCheckable(true);
  ortho_->setShortcut(QKeySequence(Qt::Key_F8));
  ortho_->setToolTip(tr("Ortho (F8): points along the construction-plane axes"));
  statusBar()->addWidget(coords_);
  statusBar()->addWidget(active_view_);
  statusBar()->addPermanentWidget(ortho_);
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
  settings.setValue(QStringLiteral("window/state"), saveState(kWindowStateVersion));
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
  auto* model = new QDockWidget(tr("Model"), this);
  model->setObjectName(QStringLiteral("modelDock"));
  model_tree_ = new ui::ModelTreePanel(doc_, selection_, model);
  model->setWidget(model_tree_);
  addDockWidget(Qt::RightDockWidgetArea, model);
  tabifyDockWidget(layers, model);
  auto* results = new QDockWidget(tr("Results"), this);
  results->setObjectName(QStringLiteral("resultsDock"));
  results->setWidget(new ui::ResultsPanel(results_, doc_, results));
  addDockWidget(Qt::RightDockWidgetArea, results);
  tabifyDockWidget(model, results);
  model->raise();
  auto* plot = new QDockWidget(tr("Plot"), this);
  plot->setObjectName(QStringLiteral("plotDock"));
  plot->setWidget(new ui::PlotWidget(results_, plot));
  addDockWidget(Qt::BottomDockWidgetArea, plot);
  plot->hide();  // appears with the first curve
  // results of the node workflow's display blocks (key "workflow:<block>") belong to their own
  // windows (double-click on the block); they open no general window
  auto own = [](const results::ResultItem* item) { return item->key.startsWith(QLatin1String("workflow:")); };
  connect(results_, &results::ResultStore::changed, this, [this, plot, own] {
    for (const auto* item : results_->visibleItems(results::ResultItem::Kind::Curve)) {
      if (!own(item)) {
        plot->show();
        return;
      }
    }
  });
  // images (camera images): their own window when the first one arrives, can be docked
  auto* image = new QDockWidget(tr("Image"), this);
  image->setObjectName(QStringLiteral("imageDock"));
  image_view_ = new ui::ImageView(results_, image);
  image->setWidget(image_view_);
  addDockWidget(Qt::RightDockWidgetArea, image);
  image->setFloating(true);
  image->resize(560, 480);
  image->hide();
  connect(results_, &results::ResultStore::changed, this, [this, image, own] {
    const auto images = results_->visibleItems(results::ResultItem::Kind::Image);
    results::ResultId newest = 0;
    for (const auto* item : images) {
      if (!own(item)) newest = item->id;
    }
    if (newest != 0 && newest != last_image_) {
      if (image->isFloating() && !image->isVisible()) {
        image->move(geometry().center() - QPoint(image->width() / 2, image->height() / 2));
      }
      image->show();
      image->raise();
    }
    last_image_ = newest;
  });
  // node workflow (RTT-Fusion): appears when the document gets a workflow
  auto* workflow = new QDockWidget(tr("Node workflow"), this);
  workflow->setObjectName(QStringLiteral("workflowDock"));
  auto* workflow_split = new QSplitter(Qt::Horizontal, workflow);
  node_editor_ = new ui::NodeEditor(workflow_split);
  auto* branches = new ui::BranchPanel(node_editor_, workflow_split);  // RTT-Fusion R24
  workflow_split->addWidget(node_editor_);
  workflow_split->addWidget(branches);
  workflow_split->setStretchFactor(0, 4);
  workflow_split->setStretchFactor(1, 1);
  workflow->setWidget(workflow_split);
  addDockWidget(Qt::BottomDockWidgetArea, workflow);
  workflow->hide();
  auto show_workflow = [this, workflow](bool fit) {
    if (node_editor_->workflow() == doc_->workflow()) return;  // our own edit
    const bool first = node_editor_->workflow()["graph"].toObject().isEmpty() &&
                       node_editor_->workflow()["model"].toObject().isEmpty();
    node_editor_->setWorkflow(doc_->workflow());
    if (doc_->workflow().isEmpty()) return;
    if (!workflow->isVisible()) {
      workflow->show();
      workflow->raise();
    }
    if (first || fit) QTimer::singleShot(0, node_editor_, &ui::NodeEditor::fitAll);
  };
  connect(doc_, &document::Document::workflowChanged, this, [show_workflow] { show_workflow(false); });
  connect(doc_, &document::Document::documentReset, this, [show_workflow] { show_workflow(true); });
  connect(node_editor_, &ui::NodeEditor::edited, doc_, &document::Document::setWorkflow);
  // plugins that follow the workflow (e.g. the Raytracer draws what an edit changed): shortly
  // after the last edit, if the plugin runs and is idle
  auto* follow = new QTimer(this);
  follow->setSingleShot(true);
  follow->setInterval(600);
  connect(node_editor_, &ui::NodeEditor::edited, follow, qOverload<>(&QTimer::start));
  connect(follow, &QTimer::timeout, this, [this] {
    if (plugins_ == nullptr) return;
    for (const auto& host : plugins_->plugins()) {
      if (!host->running() || !host->ready() || host->activeJob()) continue;
      for (const auto& a : host->actions()) {
        if (a.on_workflow_edit) plugins_->run(host.get(), a.id, doc_->pluginParameters(host->manifest().id));
      }
    }
  });
  // the block a plugin computes now
  connect(doc_, &document::Document::workflowRunning, this, [this, workflow](const QString& node) {
    node_editor_->setRunning(node);
    workflow->setWindowTitle(node.isEmpty() ? tr("Node workflow") : tr("Node workflow – running"));
  });
  // display blocks: a dockable window each, with the results the run gave them
  connect(node_editor_, &ui::NodeEditor::displayRequested, this, &MainWindow::openDisplay);
  connect(doc_, &document::Document::workflowState, node_editor_, &ui::NodeEditor::setStates);
  connect(doc_, &document::Document::documentReset, node_editor_, [this] { node_editor_->setStates({}); });
  // a plugin set the workflow (also unchanged, e.g. "Workflow from system" again after the
  // window was closed): show it
  connect(doc_, &document::Document::workflowShowRequested, this, [this, workflow] {
    if (doc_->workflow().isEmpty()) return;
    const bool was_hidden = !workflow->isVisible();
    workflow->show();
    workflow->raise();
    if (was_hidden) QTimer::singleShot(0, node_editor_, &ui::NodeEditor::fitAll);
  });
  view_menu_->addAction(model->toggleViewAction());
  view_menu_->addAction(results->toggleViewAction());
  view_menu_->addAction(workflow->toggleViewAction());
  view_menu_->addAction(image->toggleViewAction());
  view_menu_->addAction(plot->toggleViewAction());
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
  if (tool_id.startsWith(QLatin1String("plugin:"))) {  // plugin:<id>/<action>
    const QString rest = tool_id.mid(7);
    if (plugins::PluginHost* h = plugins_->plugin(rest.section(QLatin1Char('/'), 0, 0))) {
      runPluginAction(h, rest.section(QLatin1Char('/'), 1));
    }
    return;
  }
  if (commands_->start(tool_id)) return;
  statusBar()->showMessage(tr("Tool '%1' is not implemented yet (see docs/plan.md)").arg(tool_id),
                           4000);
}

void MainWindow::reportPreviousCrash() {
  const QString folder = support::CrashHandler::defaultFolder();
  const QStringList reports = support::CrashHandler::newReports(folder);
  support::CrashHandler::markSeen(folder);
  if (reports.isEmpty()) return;
  QMessageBox box(QMessageBox::Warning, tr("Raytatouille Fusion"),
                  tr("Raytatouille Fusion closed unexpectedly last time. A crash report was saved on this "
                     "computer (it is not sent anywhere):\n%1\n\nIf you report the problem, please attach it.")
                      .arg(QDir::toNativeSeparators(reports.front())),
                  QMessageBox::Ok, this);
  QPushButton* open = box.addButton(tr("Show report"), QMessageBox::ActionRole);
  box.exec();
  if (box.clickedButton() == open) QDesktopServices::openUrl(QUrl::fromLocalFile(reports.front()));
}

void MainWindow::showAbout() {
  const bool kernel_ok = kernel::self_test();
  QMessageBox::about(
      this, tr("About Raytatouille Fusion"),
      tr("<b>Raytatouille Fusion</b> %1<br>Desktop CAD front end for the Nurbforge NURBS kernel.<br><br>"
         "Nurbforge kernel %2 – self-test %3<br>Qt %4")
          .arg(QStringLiteral(NFCAD_VERSION),
               QString::fromStdString(kernel::nurbforge_version()),
               kernel_ok ? tr("passed") : tr("FAILED"), QString::fromLatin1(qVersion())));
}

QDockWidget* MainWindow::openDisplay(const QString& id, const QString& type, const QString& title) {
  QDockWidget* dock = display_docks_.value(id);
  if (dock == nullptr) {
    dock = new QDockWidget(this);
    dock->setObjectName(QStringLiteral("display_") + id);
    const QString key = QStringLiteral("workflow:") + id;
    if (type == QLatin1String("display.plot")) {
      auto* plot = new ui::PlotWidget(results_, dock);
      plot->setKey(key);
      dock->setWidget(plot);
    } else {
      auto* image = new ui::ImageView(results_, dock);
      image->setKey(key);
      dock->setWidget(image);
    }
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->setFloating(true);  // docks anywhere by dragging its title bar
    dock->resize(480, 400);
    display_docks_.insert(id, dock);
  }
  dock->setWindowTitle(tr("Display: %1").arg(title));
  dock->show();
  dock->raise();
  return dock;
}

}  // namespace nfcad::app
