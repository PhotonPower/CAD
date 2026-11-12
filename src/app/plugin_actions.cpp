// Plugins in the main window (docs/plugin-api.md): menu, tool-column groups, job progress, log.
#include <QDir>
#include <QDockWidget>
#include <QSet>
#include <QSettings>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QLabel>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <utility>

#include "app/command_controller.hpp"
#include "app/main_window.hpp"
#include "document/document.hpp"
#include "document/selection.hpp"
#include "plugins/plugin_manager.hpp"
#include "plugins/update_check.hpp"
#include "ui/plugin_panel.hpp"
#include "ui/plugin_settings_dialog.hpp"
#include "ui/tool_box.hpp"
#include "viewport/viewport.hpp"
#include "viewport/viewport_grid.hpp"

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

  // a run that fills an empty document (e.g. "Load system") shows its result in all views
  connect(plugins_, &plugins::PluginManager::jobStarted, this,
          [this](plugins::PluginHost*, int) { plugin_started_on_empty_ = doc_->entities().empty(); });

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
          [this, host](int, bool ok, bool cancelled, const QString& message, bool user_error) {
            if (active_plugin_ == host) active_plugin_ = nullptr;
            plugin_panel_->setBusy(false);
            if (std::exchange(plugin_started_on_empty_, false) && !doc_->entities().empty()) {
              QTimer::singleShot(0, this, [this] {
                for (viewport::Viewport* v : grid_->views()) v->zoomExtents();
              });
            }
            job_progress_->hide();
            job_cancel_->hide();
            const QString what = host->manifest().displayName();
            statusBar()->showMessage(cancelled ? tr("%1: cancelled").arg(what)
                                     : ok      ? tr("%1: %2").arg(what, message.isEmpty() ? tr("done") : message)
                                               : tr("%1 failed: %2").arg(what, message),
                                     ok ? 8000 : 0);
            if (user_error) {
              // something the user can fix (nfcad.UserError): say it, no traceback / log
              QMessageBox::information(this, what, message);
            } else if (!ok && !cancelled) {
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
  plugins_menu_->addAction(tr("Install plugin…"), this, [this] { installPlugin(); });
  // every plugin is listed: installed ones (Install plugin…) can be removed; plugins from a plugin
  // folder (e.g. the repository's during development) only by removing their folder
  QMenu* uninstall = plugins_menu_->addMenu(tr("Uninstall plugin"));
  uninstall->setToolTipsVisible(true);
  for (const auto& h : plugins_->plugins()) {
    const QString id = h->manifest().id;
    const QString name = h->manifest().displayName();
    const QString label = QStringLiteral("%1 %2").arg(name, h->manifest().version);
    if (!plugins_->isInstalled(h.get())) {
      QAction* a = uninstall->addAction(tr("%1 – from a plugin folder").arg(label));
      a->setEnabled(false);
      a->setToolTip(tr("Not installed with Install plugin…; it comes from %1.\nRemove that folder under "
                       "Plugins → Settings… (or delete it) to remove the plugin.")
                        .arg(QDir::toNativeSeparators(QFileInfo(h->manifest().source_directory).absolutePath())));
      continue;
    }
    uninstall->addAction(label, this, [this, id, name] {
      if (QMessageBox::question(this, tr("Uninstall plugin"), tr("Remove the plugin %1?").arg(name)) != QMessageBox::Yes) {
        return;
      }
      const QString error = plugins_->uninstall(id);
      statusBar()->showMessage(error.isEmpty() ? tr("%1 removed").arg(name) : error, 8000);
    });
  }
  if (uninstall->isEmpty()) uninstall->addAction(tr("(no plugins)"))->setEnabled(false);
  plugins_menu_->addAction(tr("Reload plugins"), this, [this] { plugins_->reload(); });
  // plugins can be kept out of the side panels (tool box, Plugin panel); their actions stay here
  const QSet<QString> hidden = hiddenPlugins();
  QMenu* show = plugins_menu_->addMenu(tr("Show in the side panels"));
  show->setToolTipsVisible(true);
  for (const auto& h : plugins_->plugins()) {
    const QString id = h->manifest().id;
    QAction* a = show->addAction(h->manifest().displayName());
    a->setCheckable(true);
    a->setChecked(!hidden.contains(id));
    a->setToolTip(tr("Tool box group and Plugin panel; the actions stay in this menu"));
    connect(a, &QAction::toggled, this, [this, id](bool on) {
      QSet<QString> ids = hiddenPlugins();
      if (on) {
        ids.remove(id);
      } else {
        ids.insert(id);
      }
      setHiddenPlugins(ids);
    });
  }
  if (show->isEmpty()) show->addAction(tr("(no plugins)"))->setEnabled(false);
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
    if (!group.tools.empty() && !hidden.contains(m.id)) groups.push_back(std::move(group));
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
  if (plugin_panel_ != nullptr) plugin_panel_->setHidden(hidden);
}

QSet<QString> MainWindow::hiddenPlugins() const {
  const QStringList ids = QSettings().value(QStringLiteral("plugins/hidden")).toStringList();
  return {ids.begin(), ids.end()};
}

void MainWindow::setHiddenPlugins(const QSet<QString>& ids) {
  QStringList list(ids.begin(), ids.end());
  list.sort();
  QSettings().setValue(QStringLiteral("plugins/hidden"), list);
  // rebuild menu, tool box and panel after the menu that triggered this has closed
  QTimer::singleShot(0, this, &MainWindow::rebuildPluginUi);
}

void MainWindow::installPlugin(const QString& package) {
  QString file = package;
  if (file.isEmpty()) {
    file = QFileDialog::getOpenFileName(this, tr("Install plugin"), {},
                                        tr("Plugin packages (*%1)").arg(QLatin1String(plugins::kPackageSuffix)));
  }
  if (file.isEmpty()) return;
  if (active_plugin_ != nullptr) {
    statusBar()->showMessage(tr("A plugin job is still running"), 4000);
    return;
  }
  const plugins::Installation r = plugins_->install(file);
  if (!r.error.isEmpty()) {
    QMessageBox::warning(this, tr("Install plugin"), r.error);
    return;
  }
  QString text = r.replaced.isEmpty()
                     ? tr("%1 %2 installed.").arg(r.manifest.name, r.manifest.version)
                     : tr("%1 updated from %2 to %3.").arg(r.manifest.name, r.replaced, r.manifest.version);
  if (!r.signature.ok()) text += QLatin1Char('\n') + tr("Not signed (%1) – it only loads in developer mode.").arg(r.signature.message);
  // the Python packages the plugin needs
  const plugins::EnvironmentReport env = plugins::check_environment(plugins_->python(), r.manifest.requirements);
  if (!env.ok()) {
    text += QStringLiteral("\n\n") + (env.error.isEmpty() ? tr("Missing Python packages – install them with:\n%1").arg(env.pipCommand())
                                                          : env.error);
  }
  QMessageBox::information(this, tr("Install plugin"), text);
}

void MainWindow::checkForUpdates() {
  using plugins::UpdateChecker;
  std::vector<UpdateChecker::Target> targets;
  targets.push_back({tr("Raytatouille Fusion"), QStringLiteral(NFCAD_VERSION), QUrl(QString::fromLatin1(plugins::kReleasesApi)), true});
  for (const auto& h : plugins_->plugins()) {
    if (!h->manifest().update_url.isEmpty()) {
      targets.push_back({h->manifest().name, h->manifest().version, QUrl(h->manifest().update_url), false});
    }
  }
  statusBar()->showMessage(tr("Checking for updates …"));
  auto* checker = new UpdateChecker(this);
  connect(checker, &UpdateChecker::finished, this, [this, checker](const std::vector<plugins::UpdateInfo>& results) {
    checker->deleteLater();
    statusBar()->clearMessage();
    QStringList lines;
    for (const plugins::UpdateInfo& u : results) {
      if (!u.error.isEmpty()) {
        lines << tr("%1 %2: check failed (%3)").arg(u.what, u.installed, u.error.toHtmlEscaped());
      } else if (u.newer()) {
        lines << tr("<b>%1</b>: %2 available (installed %3) – <a href=\"%4\">download</a>")
                     .arg(u.what, u.available, u.installed, u.url.toString(QUrl::FullyEncoded).toHtmlEscaped());
      } else {
        lines << tr("%1 %2 is up to date").arg(u.what, u.installed);
      }
    }
    QMessageBox box(QMessageBox::Information, tr("Updates"), lines.join(QStringLiteral("<br>")), QMessageBox::Ok, this);
    box.setTextFormat(Qt::RichText);
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.exec();
  });
  checker->check(targets);
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
