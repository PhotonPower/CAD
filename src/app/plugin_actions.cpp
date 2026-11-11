// Plugins in the main window (docs/plugin-api.md): menu, tool-column groups, job progress, log.
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QStatusBar>
#include <QToolButton>

#include "app/command_controller.hpp"
#include "app/main_window.hpp"
#include "document/document.hpp"
#include "document/selection.hpp"
#include "plugins/plugin_manager.hpp"
#include "ui/plugin_panel.hpp"
#include "ui/plugin_settings_dialog.hpp"
#include "ui/tool_box.hpp"

namespace nfcad::app {

void MainWindow::createPlugins() {
  plugins_ = new plugins::PluginManager(doc_, selection_, results_, this);

  // menu (before Help)
  plugins_menu_ = new QMenu(tr("&Plugins"), this);
  menuBar()->insertMenu(menuBar()->actions().constLast(), plugins_menu_);

  // log dock (bottom, next to the plot)
  plugin_log_ = new QPlainTextEdit(this);
  plugin_log_->setReadOnly(true);
  plugin_log_->setMaximumBlockCount(5000);
  auto* log_dock = new QDockWidget(tr("Plugin log"), this);
  log_dock->setObjectName(QStringLiteral("pluginLogDock"));
  log_dock->setWidget(plugin_log_);
  addDockWidget(Qt::BottomDockWidgetArea, log_dock);
  if (auto* plot = findChild<QDockWidget*>(QStringLiteral("plotDock"))) tabifyDockWidget(plot, log_dock);
  log_dock->hide();
  view_menu_->addAction(log_dock->toggleViewAction());

  // job progress in the status bar
  job_progress_ = new QProgressBar(this);
  job_progress_->setRange(0, 1000);
  job_progress_->setMaximumWidth(180);
  job_progress_->setTextVisible(false);
  job_cancel_ = new QToolButton(this);
  job_cancel_->setText(tr("Cancel"));
  statusBar()->addPermanentWidget(job_progress_);
  statusBar()->addPermanentWidget(job_cancel_);
  job_progress_->hide();
  job_cancel_->hide();
  connect(job_cancel_, &QToolButton::clicked, this, [this] {
    if (active_plugin_ != nullptr) active_plugin_->cancel();
  });

  // parameter panel (right dock, tab next to the results)
  ui::PluginForm::Callbacks callbacks;
  callbacks.selection = [this](const QStringList& kinds) {
    static const QStringList names = {QStringLiteral("point"), QStringLiteral("curve"), QStringLiteral("surface"),
                                      QStringLiteral("trimmed_surface"), QStringLiteral("shell")};
    std::vector<std::pair<qint64, QString>> out;
    for (const document::SelectionItem& i : selection_->ordered()) {
      if (i.sub.kind != document::SubElement::Kind::None) continue;
      const document::Entity* e = doc_->entity(i.entity);
      if (e == nullptr) continue;
      const QString kind = names[static_cast<int>(document::kind_of(*e->geometry))];
      if (!kinds.isEmpty() && !kinds.contains(kind)) continue;
      out.emplace_back(static_cast<qint64>(e->id), e->name.isEmpty() ? QStringLiteral("#%1").arg(e->id) : e->name);
    }
    if (out.empty()) statusBar()->showMessage(tr("Select suitable objects first (%1)").arg(kinds.join(QStringLiteral(", "))), 5000);
    return out;
  };
  callbacks.entity_name = [this](qint64 id) {
    const document::Entity* e = doc_->entity(static_cast<document::EntityId>(id));
    if (e == nullptr) return QString();
    return e->name.isEmpty() ? QStringLiteral("#%1").arg(id) : e->name;
  };
  callbacks.pick_point = [this](std::function<void(double, double, double)> done) {
    commands_->pickPoint(tr("Pick a point for the plugin parameter (snaps active, Esc cancels)"),
                         [done](const commands::Point3& p) { done(p.x, p.y, p.z); });
  };
  plugin_panel_ = new ui::PluginPanel(plugins_, doc_, callbacks,
                                      [this](plugins::PluginHost* host, const QString& action) {
                                        runPluginAction(host, action);
                                      });
  auto* panel_dock = new QDockWidget(tr("Plugin"), this);
  panel_dock->setObjectName(QStringLiteral("pluginDock"));
  panel_dock->setWidget(plugin_panel_);
  addDockWidget(Qt::RightDockWidgetArea, panel_dock);
  if (auto* results = findChild<QDockWidget*>(QStringLiteral("resultsDock"))) tabifyDockWidget(results, panel_dock);
  if (auto* model = findChild<QDockWidget*>(QStringLiteral("modelDock"))) model->raise();
  view_menu_->addAction(panel_dock->toggleViewAction());

  connect(plugins_, &plugins::PluginManager::pluginsChanged, this, [this] {
    for (const auto& h : plugins_->plugins()) {
      connectPlugin(h.get());
      // start right away so that the actions (known after the plugin's hello) appear
      h->start(plugins_->python(), plugins::PluginManager::nfcadPythonPath());
    }
    rebuildPluginUi();
  });
  plugins_->reload();
}

void MainWindow::connectPlugin(plugins::PluginHost* host) {
  connect(host, &plugins::PluginHost::readyChanged, this, &MainWindow::rebuildPluginUi);
  connect(host, &plugins::PluginHost::logLine, this, [this, host](const QString& line) {
    plugin_log_->appendPlainText(QStringLiteral("[%1] %2").arg(host->manifest().id, line));
  });
  connect(host, &plugins::PluginHost::progress, this, [this](int, double value, const QString& text) {
    job_progress_->setValue(static_cast<int>(value * 1000));
    if (!text.isEmpty()) statusBar()->showMessage(text);
  });
  connect(host, &plugins::PluginHost::finished, this,
          [this, host](int, bool ok, bool cancelled, const QString& message) {
            if (active_plugin_ == host) active_plugin_ = nullptr;
            plugin_panel_->setBusy(false);
            job_progress_->hide();
            job_cancel_->hide();
            const QString what = host->manifest().displayName();
            statusBar()->showMessage(cancelled ? tr("%1: cancelled").arg(what)
                                     : ok      ? tr("%1: %2").arg(what, message.isEmpty() ? tr("done") : message)
                                               : tr("%1 failed: %2").arg(what, message),
                                     ok ? 8000 : 0);
            if (!ok && !cancelled) {
              if (auto* d = findChild<QDockWidget*>(QStringLiteral("pluginLogDock"))) {
                d->show();
                d->raise();
              }
            }
          });
  connect(host, &plugins::PluginHost::failed, this, [this, host](const QString& message) {
    plugin_log_->appendPlainText(QStringLiteral("[%1] %2").arg(host->manifest().id, message));
    statusBar()->showMessage(message.section(QLatin1Char('\n'), 0, 0), 0);
    rebuildPluginUi();
  });
}

void MainWindow::rebuildPluginUi() {
  plugins_menu_->clear();
  plugins_menu_->addAction(tr("Settings…"), this, [this] {
    ui::PluginSettingsDialog dialog(plugins_, this);
    dialog.exec();
  });
  plugins_menu_->addAction(tr("Reload plugins"), this, [this] { plugins_->reload(); });
  plugins_menu_->addSeparator();
  std::vector<ui::ToolGroupDef> groups;
  for (const auto& h : plugins_->plugins()) {
    const auto& m = h->manifest();
    QMenu* sub = plugins_menu_->addMenu(m.displayName());
    sub->setToolTip(m.description);
    ui::ToolGroupDef group{QStringLiteral("plugin_") + m.id, m.displayName(), true, {}};
    if (!h->ready()) {
      sub->addAction(h->running() ? tr("(starting …)") : tr("(not running – see the plugin log)"))->setEnabled(false);
    }
    for (const auto& a : h->actions()) {
      plugins::PluginHost* host = h.get();
      const QString action = a.id;
      sub->addAction(a.label, this, [this, host, action] { runPluginAction(host, action); });
      group.tools.push_back({QStringLiteral("plugin:%1/%2").arg(m.id, a.id), a.label,
                             a.label.left(3), QKeySequence()});
    }
    if (!group.tools.empty()) groups.push_back(std::move(group));
  }
  if (plugins_->plugins().empty()) {
    plugins_menu_->addAction(tr("(no plugins loaded – see Settings)"))->setEnabled(false);
  }
  // developer mode (P5): unsigned plugins are loaded – always visible
  if (developer_banner_ == nullptr) {
    developer_banner_ = new QLabel(tr(" DEVELOPER MODE – unsigned plugins allowed "), this);
    developer_banner_->setStyleSheet(QStringLiteral("background: #b02a2a; color: white; font-weight: bold;"));
    developer_banner_->setToolTip(tr("Plugins → Settings… → Developer mode"));
    statusBar()->addPermanentWidget(developer_banner_);
  }
  developer_banner_->setVisible(plugins_->developerMode());
  toolbox_->setExtraGroups(groups);
}

void MainWindow::runPluginAction(plugins::PluginHost* host, const QString& action) {
  if (active_plugin_ != nullptr) {
    statusBar()->showMessage(tr("A plugin job is still running"), 4000);
    return;
  }
  if (plugins_->run(host, action, doc_->pluginParameters(host->manifest().id)) == 0) return;
  active_plugin_ = host;
  plugin_panel_->setBusy(true);
  job_progress_->setValue(0);
  job_progress_->show();
  job_cancel_->show();
  statusBar()->showMessage(tr("%1: %2 …").arg(host->manifest().displayName(), action));
}

}  // namespace nfcad::app
