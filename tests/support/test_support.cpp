#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <stdexcept>
#include <string>

#include "support/crash_handler.hpp"
#include "support/settings_migration.hpp"

using nfcad::support::CrashHandler;

class TestSupport : public QObject {
  Q_OBJECT
  QTemporaryDir tmp_;

  /// Starts this program as a child that installs the handler and crashes; returns the report.
  QString crashChild(const QString& how) {
    const QString folder = tmp_.filePath(how);
    QProcess p;
    p.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--crash"), how, folder});
    if (!p.waitForFinished(30000)) return QStringLiteral("(child did not end)");
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) return QStringLiteral("(child did not crash)");
    const QStringList reports = CrashHandler::newReports(folder);
    if (reports.size() != 1) return QStringLiteral("(%1 reports)").arg(reports.size());
    QFile f(reports.front());
    if (!f.open(QIODevice::ReadOnly)) return QStringLiteral("(unreadable)");
    return QString::fromUtf8(f.readAll());
  }

 private slots:
  void segfaultIsReported() {
    const QString report = crashChild(QStringLiteral("segv"));
    QVERIFY2(report.contains(QStringLiteral("SIGSEGV")) ||
                 report.contains(QStringLiteral("Windows exception 0xc0000005")),
             qPrintable(report));
    QVERIFY(report.contains(QStringLiteral("crash report (local only")));
    QVERIFY(report.contains(QStringLiteral("Backtrace:")));
  }

  void uncaughtExceptionIsReported() {
    const QString report = crashChild(QStringLiteral("throw"));
    QVERIFY2(report.contains(QStringLiteral("std::terminate")), qPrintable(report));
  }

  void reportsAreShownOnce() {
    const QString folder = tmp_.filePath(QStringLiteral("segv"));
    QCOMPARE(CrashHandler::newReports(folder).size(), 1);
    CrashHandler::markSeen(folder);
    QVERIFY(CrashHandler::newReports(folder).isEmpty());
  }

  void settingsMigration() {
    QCoreApplication::setOrganizationName(QStringLiteral("PhotonPower-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_support"));
    const QString org = QStringLiteral("PhotonPower-tests-old");
    QSettings old(org, QStringLiteral("Nurbforge CAD"));
    old.setValue(QStringLiteral("plugins/python"), QStringLiteral("/old/python"));
    old.setValue(QStringLiteral("window/state"), 42);
    old.sync();
    QSettings now;
    now.clear();
    now.setValue(QStringLiteral("window/state"), 7);  // already set: kept
    QCOMPARE(nfcad::support::migrate_settings(now, {org}), 0);
    QCOMPARE(now.value(QStringLiteral("plugins/python")).toString(), QStringLiteral("/old/python"));
    QCOMPARE(now.value(QStringLiteral("window/state")).toInt(), 7);
    QCOMPARE(now.value(QStringLiteral("settings/version")).toInt(), nfcad::support::kSettingsVersion);
    // once only
    now.remove(QStringLiteral("plugins/python"));
    QCOMPARE(nfcad::support::migrate_settings(now, {org}), 1);
    QVERIFY(!now.contains(QStringLiteral("plugins/python")));
    // the marker of the 0.0.x rename counts as version 1
    now.clear();
    now.setValue(QStringLiteral("migrated/from-nurbforge-cad"), true);
    QCOMPARE(nfcad::support::migrate_settings(now, {org}), 1);
    QVERIFY(!now.contains(QStringLiteral("plugins/python")));
    QVERIFY(!now.contains(QStringLiteral("migrated/from-nurbforge-cad")));
    now.clear();
    old.clear();
  }
};

int main(int argc, char* argv[]) {
  // child mode: crash on purpose after installing the handler
  if (argc == 4 && std::string(argv[1]) == "--crash") {
    QCoreApplication app(argc, argv);
    CrashHandler::install(QString::fromLocal8Bit(argv[3]), QStringLiteral("test"));
    if (std::string(argv[2]) == "segv") {
      volatile int* p = nullptr;
      *p = 1;  // NOLINT: deliberate
    }
    throw std::runtime_error("deliberate");
  }
  QCoreApplication app(argc, argv);
  TestSupport t;
  return QTest::qExec(&t, argc, argv);
}

#include "test_support.moc"
