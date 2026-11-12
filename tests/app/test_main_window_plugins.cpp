#include <QDockWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTest>
#include <QTreeWidget>
#include <memory>

#include "app/main_window.hpp"
#include "document/document.hpp"
#include "document/entity.hpp"
#include "plugins/plugin_manager.hpp"
#include "ui/plugin_settings_dialog.hpp"
#include "ui/results_panel.hpp"
#include "ui/tool_box.hpp"

class TestMainWindowPlugins : public QObject {
  Q_OBJECT
 private slots:
  void closingWithUndoHistory() {
    // regression (M8): destroying the window with an undo history emitted cleanChanged into
    // the half-destroyed window (assert in debug builds, undefined behaviour in release)
    QStandardPaths::setTestModeEnabled(true);
    auto w = std::make_unique<nfcad::app::MainWindow>();
    auto* doc = w->findChild<nfcad::document::Document*>();
    doc->addEntities({nfcad::document::make_entity(nf::Vec3{1, 2, 3}, QStringLiteral("p"))}, QStringLiteral("add"));
    QVERIFY(doc->isModified());
    w.reset();  // must not crash
  }

  void workflowWindowComesBack() {
    // the node workflow dock opens when a plugin sets a workflow, and again after the user
    // closed it, even if the workflow did not change ("Workflow from system" twice)
    QStandardPaths::setTestModeEnabled(true);
    nfcad::app::MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen);
    w.show();
    auto* doc = w.findChild<nfcad::document::Document*>();
    auto* dock = w.findChild<QDockWidget*>(QStringLiteral("workflowDock"));
    QVERIFY(doc != nullptr && dock != nullptr);
    QVERIFY(!dock->isVisible());
    const QJsonObject wf{{"graph", QJsonObject{{"format", "rtt-fusion-graph"}, {"version", 1},
                                               {"nodes", QJsonArray{QJsonObject{{"id", "a"}, {"type", "rtt.source"}}}},
                                               {"links", QJsonArray{}}}}};
    doc->setWorkflow(wf);
    doc->requestWorkflowShown();
    QVERIFY(dock->isVisible());
    dock->close();
    QVERIFY(!dock->isVisible());
    doc->setWorkflow(wf);  // unchanged: no workflowChanged
    doc->requestWorkflowShown();
    QVERIFY(dock->isVisible());
    // the View menu reopens it as well
    dock->close();
    dock->toggleViewAction()->trigger();
    QVERIFY(dock->isVisible());
  }

  void demoPluginFromTheMenu() {
    if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty() &&
        QStandardPaths::findExecutable(QStringLiteral("python")).isEmpty() && qEnvironmentVariableIsEmpty("NFCAD_PYTHON")) {
      QSKIP("no Python interpreter found");
    }
    QStandardPaths::setTestModeEnabled(true);  // settings of this test only
    qputenv("NFCAD_PLUGIN_PATH", QByteArray(NFCAD_EXAMPLES_DIR));
    qputenv("NFCAD_DEVELOPER_MODE", "1");  // the example plugin in the repository is not signed
    nfcad::app::MainWindow w;
    bool banner = false;  // developer mode is always visible
    for (auto* l : w.statusBar()->findChildren<QLabel*>()) banner = banner || (l->text().contains(QStringLiteral("DEVELOPER MODE")) && !l->isHidden());
    QVERIFY(banner);
    // the plugin menu fills once the demo plugin said hello
    QMenu* plugins = nullptr;
    for (QAction* a : w.menuBar()->actions()) {
      if (a->menu() != nullptr && a->text().contains(QStringLiteral("Plugins"))) plugins = a->menu();
    }
    QVERIFY(plugins != nullptr);
    QAction* rays = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
          for (QAction* a : plugins->actions()) {
            if (a->menu() == nullptr) continue;
            for (QAction* b : a->menu()->actions()) {
              if (b->text() == QStringLiteral("Ray fan from the origin")) rays = b;
            }
          }
          return rays != nullptr;
        }(),
        20000);
    // Uninstall lists the plugin from the folder, greyed out with where it comes from
    QMenu* uninstall = nullptr;
    for (QAction* a : plugins->actions()) {
      if (a->menu() != nullptr && a->text().contains(QStringLiteral("Uninstall"))) uninstall = a->menu();
    }
    QVERIFY(uninstall != nullptr);
    bool listed = false;
    for (QAction* a : uninstall->actions()) {
      if (a->text().contains(QStringLiteral("Demo plugin"))) {
        listed = true;
        QVERIFY(!a->isEnabled());
        QVERIFY(a->toolTip().contains(QStringLiteral("examples")));
      }
    }
    QVERIFY(listed);
    // the plugin also got a group in the tool column
    auto* toolbox = w.findChild<nfcad::ui::ToolBox*>();
    QVERIFY(toolbox != nullptr);
    bool group = false;
    for (auto* g : toolbox->findChildren<nfcad::ui::ToolGroup*>()) group = group || g->groupId() == QStringLiteral("plugin_demo");
    QVERIFY(group);

    rays->trigger();
    auto* tree = w.findChild<nfcad::ui::ResultsPanel*>()->findChild<QTreeWidget*>();
    QTRY_COMPARE_WITH_TIMEOUT(tree->topLevelItemCount(), 1, 20000);
    QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Demo rays"));
    QTRY_VERIFY_WITH_TIMEOUT(w.statusBar()->currentMessage().contains(QStringLiteral("300 rays")), 10000);

    // the settings dialog: environment check against the plugins' requirements
    nfcad::ui::PluginSettingsDialog dialog(w.findChild<nfcad::plugins::PluginManager*>(), &w);
    for (auto* b : dialog.findChildren<QPushButton*>()) {
      if (b->text() == QStringLiteral("Check environment")) b->click();
    }
    auto* report = dialog.findChild<QPlainTextEdit*>();
    QVERIFY(report->toPlainText().contains(QStringLiteral("numpy")));
    if (const QByteArray dir = qgetenv("NFCAD_SNAPSHOT_DIR"); !dir.isEmpty()) {
      dialog.grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/plugin_settings.png"));
    }
  }
};

QTEST_MAIN(TestMainWindowPlugins)
#include "test_main_window_plugins.moc"
