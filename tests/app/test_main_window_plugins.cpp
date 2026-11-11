#include <QLabel>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTest>
#include <QTreeWidget>

#include "app/main_window.hpp"
#include "plugins/plugin_manager.hpp"
#include "ui/plugin_settings_dialog.hpp"
#include "ui/results_panel.hpp"
#include "ui/tool_box.hpp"

class TestMainWindowPlugins : public QObject {
  Q_OBJECT
 private slots:
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
