#include <QDockWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QComboBox>
#include <QSettings>
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
#include "ui/plugin_panel.hpp"
#include "ui/plugin_settings_dialog.hpp"
#include "results/result_store.hpp"
#include "ui/image_view.hpp"
#include "ui/node_editor.hpp"
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

  void escapeLeavesAGroup() {
    // Escape belongs to the node editor while it is inside a group (the main window's Escape,
    // "no tool", would take it first); the level's name at the top left goes back too
    QStandardPaths::setTestModeEnabled(true);
    nfcad::app::MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen);
    w.show();
    auto* doc = w.findChild<nfcad::document::Document*>();
    auto* editor = w.findChild<nfcad::ui::NodeEditor*>();
    QVERIFY(doc != nullptr && editor != nullptr);
    const QJsonObject inner{{"nodes", QJsonArray{QJsonObject{{"id", "m"}, {"type", "rtt.mirror"}}}},
                            {"connections", QJsonArray{}}, {"links", QJsonArray{}}, {"branches", QJsonArray{}},
                            {"ports", QJsonObject{}}};
    const QJsonObject model{{"format", "rtt-fusion-graph"}, {"version", 2},
                            {"nodes", QJsonArray{QJsonObject{{"id", "g"}, {"type", "group"}, {"variant", "main"},
                                                             {"variants", QJsonObject{{"main", inner}}}}}},
                            {"connections", QJsonArray{}}, {"links", QJsonArray{}}, {"branches", QJsonArray{}}};
    doc->setWorkflow(QJsonObject{{"model", model}});
    doc->requestWorkflowShown();
    QVERIFY(editor->enterGroup(QStringLiteral("g")));
    // the main window active, as in use: only then its Escape shortcut competes
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&w));
    editor->setFocus();
    QTest::keyClick(editor, Qt::Key_Escape);  // through the shortcut map, as a real key press
    QVERIFY(editor->scope().isEmpty());
    QVERIFY(editor->enterGroup(QStringLiteral("g")));
    auto* crumb = editor->viewport()->findChild<QLabel*>();
    QVERIFY(crumb != nullptr && crumb->isVisible());
    QTest::mouseClick(crumb, Qt::LeftButton);
    QVERIFY(editor->scope().isEmpty());
  }

  void displayWindow() {
    // a display block's window shows the results with its key, dockable
    QStandardPaths::setTestModeEnabled(true);
    nfcad::app::MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen);
    w.show();
    auto* store = w.findChild<nfcad::results::ResultStore*>();
    QVERIFY(store != nullptr);
    const auto set = store->createSet(QStringLiteral("Workflow"));
    nfcad::results::Image im;
    im.width = 2;
    im.height = 2;
    im.values = {0, 1, 2, 3};
    im.x1 = 1;
    im.y1 = 1;
    store->addImage(set, QStringLiteral("other"), im);
    const auto keyed = store->addImage(set, QStringLiteral("camera"), im);
    store->setKey(keyed, QStringLiteral("workflow:show"));
    QDockWidget* dock = w.openDisplay(QStringLiteral("show"), QStringLiteral("display.image"), QStringLiteral("Camera"));
    QVERIFY(dock != nullptr && dock->isVisible());
    QVERIFY(dock->windowTitle().contains(QStringLiteral("Camera")));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetFloatable) && dock->features().testFlag(QDockWidget::DockWidgetMovable));
    auto* view = qobject_cast<nfcad::ui::ImageView*>(dock->widget());
    QVERIFY(view != nullptr && view->current() != nullptr);
    QCOMPARE(view->current()->id, keyed);
    QCOMPARE(w.openDisplay(QStringLiteral("show"), QStringLiteral("display.image"), QStringLiteral("Camera")), dock);  // one per block
  }

  void displayResultsOpenNoGeneralWindow() {
    // results of display blocks (key "workflow:…") belong to their own windows: the general
    // image and plot windows do not pop up for them, as they do for other results
    QStandardPaths::setTestModeEnabled(true);
    nfcad::app::MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen);
    w.show();
    auto* store = w.findChild<nfcad::results::ResultStore*>();
    auto dock = [&w](const char* name) { return w.findChild<QDockWidget*>(QString::fromLatin1(name)); };
    QVERIFY(store != nullptr && dock("imageDock") != nullptr && dock("plotDock") != nullptr);
    const auto set = store->createSet(QStringLiteral("Workflow"));
    nfcad::results::Image im;
    im.width = 2;
    im.height = 2;
    im.values = {0, 1, 2, 3};
    im.x1 = 1;
    im.y1 = 1;
    nfcad::results::Curve c;
    c.x = {0, 1};
    c.y = {1, 2};
    store->addImage(set, QStringLiteral("camera"), im, QStringLiteral("workflow:show"));
    store->addCurve(set, QStringLiteral("sweep"), c, QStringLiteral("workflow:plot"));
    QVERIFY(!dock("imageDock")->isVisible());
    QVERIFY(!dock("plotDock")->isVisible());
    store->addImage(set, QStringLiteral("trace"), im);  // an ordinary result: shown
    store->addCurve(set, QStringLiteral("merit"), c);
    QVERIFY(dock("imageDock")->isVisible());
    QVERIFY(dock("plotDock")->isVisible());
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

    // Plugins → Show in the side panels: a hidden plugin leaves the tool box and the Plugin
    // panel, its actions stay in the menu; the choice is kept in the settings
    auto menu_named = [&](QMenu* in, const QString& text) -> QMenu* {
      for (QAction* a : in->actions()) {
        if (a->menu() != nullptr && a->text().contains(text)) return a->menu();
      }
      return nullptr;
    };
    QMenu* show = menu_named(plugins, QStringLiteral("Show in the side panels"));
    QVERIFY(show != nullptr);
    QAction* demo = nullptr;
    for (QAction* a : show->actions()) {
      if (a->text().contains(QStringLiteral("Demo plugin"))) demo = a;
    }
    QVERIFY(demo != nullptr && demo->isCheckable() && demo->isChecked());
    demo->setChecked(false);
    auto has_group = [&] {
      bool found = false;
      for (auto* g : toolbox->findChildren<nfcad::ui::ToolGroup*>()) found = found || g->groupId() == QStringLiteral("plugin_demo");
      return found;
    };
    QTRY_VERIFY_WITH_TIMEOUT(!has_group(), 5000);
    auto* chooser = w.findChild<nfcad::ui::PluginPanel*>()->findChild<QComboBox*>();
    QCOMPARE(chooser->findData(QStringLiteral("demo")), -1);
    QMenu* demo_menu = menu_named(plugins, QStringLiteral("Demo plugin"));
    QVERIFY(demo_menu != nullptr && !demo_menu->actions().isEmpty());
    // and back
    show = menu_named(plugins, QStringLiteral("Show in the side panels"));
    for (QAction* a : show->actions()) {
      if (a->text().contains(QStringLiteral("Demo plugin"))) a->setChecked(true);
    }
    QTRY_VERIFY_WITH_TIMEOUT(has_group(), 5000);
    QVERIFY(chooser->findData(QStringLiteral("demo")) >= 0);
  }
};

QTEST_MAIN(TestMainWindowPlugins)
#include "test_main_window_plugins.moc"
