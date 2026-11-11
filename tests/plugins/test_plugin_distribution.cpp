#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <private/qzipwriter_p.h>

#include "document/document.hpp"
#include "document/selection.hpp"
#include "plugins/distribution.hpp"
#include "plugins/plugin_manager.hpp"
#include "plugins/signature.hpp"
#include "plugins/update_check.hpp"
#include "results/result_store.hpp"

using namespace nfcad::plugins;

namespace {
QString find_python() {
  const QString env = qEnvironmentVariable("NFCAD_TEST_PYTHON");
  if (!env.isEmpty()) return env;
  for (const char* n : {"python3", "python"}) {
    const QString p = QStandardPaths::findExecutable(QString::fromLatin1(n));
    if (!p.isEmpty()) return p;
  }
  return {};
}

void write(const QString& path, const QByteArray& data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(data);
}

/// A minimal plugin folder with the given id / version.
void make_plugin(const QString& dir, const QString& id, const QString& version) {
  write(dir + QStringLiteral("/plugin.json"),
        QJsonDocument(QJsonObject{{"id", id}, {"name", id}, {"version", version}, {"api", 1},
                                  {"entry", "p:P"}, {"requires", QJsonArray{"numpy"}}})
            .toJson());
  write(dir + QStringLiteral("/p.py"), "from nfcad import Plugin\nclass P(Plugin):\n    pass\n");
  write(dir + QStringLiteral("/data/table.txt"), "1 2 3\n");
}
}  // namespace

class TestPluginDistribution : public QObject {
  Q_OBJECT
  QTemporaryDir tmp_;
  KeyPair key_ = generate_key_pair();
  QStringList trusted_;

  QString path(const QString& rel) const { return tmp_.filePath(rel); }

 private slots:
  void initTestCase() {
    trusted_ = {format_public_key(key_.public_key)};
    QCoreApplication::setOrganizationName(QStringLiteral("PhotonPower-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_plugin_distribution"));
    QSettings().clear();
  }
  void cleanupTestCase() { QSettings().clear(); }

  void versions() {
    QCOMPARE(compare_versions(QStringLiteral("0.10.0"), QStringLiteral("0.9.3")), 1);
    QCOMPARE(compare_versions(QStringLiteral("1.0"), QStringLiteral("1.0.0")), 0);
    QCOMPARE(compare_versions(QStringLiteral("v1.2"), QStringLiteral("1.2")), 0);
    QCOMPARE(compare_versions(QStringLiteral("1.0-rc1"), QStringLiteral("1.0")), -1);
    QCOMPARE(compare_versions(QStringLiteral("2"), QStringLiteral("10")), -1);
    const auto r = parse_requirement(QStringLiteral("raytatouille>=0.9"));
    QVERIFY(r.has_value());
    QCOMPARE(r->name, QStringLiteral("raytatouille"));
    QCOMPARE(r->op, QStringLiteral(">="));
    QCOMPARE(r->version, QStringLiteral("0.9"));
    QVERIFY(parse_requirement(QStringLiteral("numpy"))->op.isEmpty());
    QVERIFY(!parse_requirement(QStringLiteral("numpy; rm -rf /")).has_value());
  }

  void packInstallUpdateUninstall() {
    const QString src = path(QStringLiteral("src/alpha"));
    make_plugin(src, QStringLiteral("alpha"), QStringLiteral("1.0.0"));
    QCOMPARE(sign_plugin(src, key_.seed), QString());
    QCOMPARE(pack_plugin(src, path(QStringLiteral("alpha-1.nfplugin"))), QString());
    const QString root = path(QStringLiteral("installed"));
    Installation r = install_plugin(path(QStringLiteral("alpha-1.nfplugin")), root, trusted_, false);
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QVERIFY(r.signature.ok());
    QVERIFY(r.replaced.isEmpty());
    QCOMPARE(r.directory, QDir(root).filePath(QStringLiteral("alpha")));
    QVERIFY(QFileInfo::exists(r.directory + QStringLiteral("/data/table.txt")));
    QVERIFY(verify_plugin(r.directory, trusted_).ok());  // installed with its signature
    QCOMPARE(r.manifest.requirements, QStringList{QStringLiteral("numpy")});
    // a newer version replaces it
    make_plugin(src, QStringLiteral("alpha"), QStringLiteral("1.1.0"));
    QFile::remove(src + QStringLiteral("/data/table.txt"));
    QCOMPARE(sign_plugin(src, key_.seed), QString());
    QCOMPARE(pack_plugin(src, path(QStringLiteral("alpha-2.nfplugin"))), QString());
    r = install_plugin(path(QStringLiteral("alpha-2.nfplugin")), root, trusted_, false);
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QCOMPARE(r.replaced, QStringLiteral("1.0.0"));
    QCOMPARE(r.manifest.version, QStringLiteral("1.1.0"));
    QVERIFY(!QFileInfo::exists(r.directory + QStringLiteral("/data/table.txt")));  // old files gone
    QCOMPARE(uninstall_plugin(root, QStringLiteral("alpha")), QString());
    QVERIFY(!QFileInfo::exists(r.directory));
    QVERIFY(!uninstall_plugin(root, QStringLiteral("alpha")).isEmpty());
    QVERIFY(!uninstall_plugin(root, QStringLiteral("../installed")).isEmpty());
  }

  void unsignedAndTampered() {
    const QString src = path(QStringLiteral("src/beta"));
    make_plugin(src, QStringLiteral("beta"), QStringLiteral("1.0"));
    QCOMPARE(pack_plugin(src, path(QStringLiteral("beta.nfplugin"))), QString());
    const QString root = path(QStringLiteral("installed2"));
    Installation r = install_plugin(path(QStringLiteral("beta.nfplugin")), root, trusted_, false);
    QVERIFY(r.error.contains(QStringLiteral("only signed")));
    QVERIFY(!QFileInfo::exists(QDir(root).filePath(QStringLiteral("beta"))));
    r = install_plugin(path(QStringLiteral("beta.nfplugin")), root, trusted_, true);  // developer mode
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QCOMPARE(r.signature.status, Verification::Status::Unsigned);
    // signed, then changed: refused
    QCOMPARE(sign_plugin(src, key_.seed), QString());
    write(src + QStringLiteral("/p.py"), "import os  # changed\n");
    QCOMPARE(pack_plugin(src, path(QStringLiteral("beta-t.nfplugin"))), QString());
    r = install_plugin(path(QStringLiteral("beta-t.nfplugin")), root, trusted_, false);
    QVERIFY(r.error.contains(QStringLiteral("modified after signing")));
  }

  void maliciousArchivesAreRefused() {
    const QString root = path(QStringLiteral("installed3"));
    auto archive = [&](const QString& name, const QList<QPair<QString, QByteArray>>& files) {
      QFile out(path(name));
      QVERIFY(out.open(QIODevice::WriteOnly));
      QZipWriter zip(&out);
      for (const auto& [p, data] : files) zip.addFile(p, data);
      zip.close();
    };
    const QByteArray manifest = "{\"id\": \"evil\", \"version\": \"1\", \"api\": 1, \"entry\": \"p:P\"}";
    archive(QStringLiteral("slip.nfplugin"), {{"evil/plugin.json", manifest}, {"evil/../../outside.py", "x"}});
    Installation r = install_plugin(path(QStringLiteral("slip.nfplugin")), root, trusted_, true);
    QVERIFY2(r.error.contains(QStringLiteral("invalid path")), qPrintable(r.error));
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("outside.py"))));
    QVERIFY(!QFileInfo::exists(QDir(root).filePath(QStringLiteral("evil"))));
    archive(QStringLiteral("abs.nfplugin"), {{"plugin.json", manifest}, {"/etc/x", "x"}});
    QVERIFY(!install_plugin(path(QStringLiteral("abs.nfplugin")), root, trusted_, true).error.isEmpty());
    const QByteArray bad_id = "{\"id\": \"../up\", \"version\": \"1\", \"api\": 1, \"entry\": \"p:P\"}";
    archive(QStringLiteral("id.nfplugin"), {{"plugin.json", bad_id}});
    QVERIFY(install_plugin(path(QStringLiteral("id.nfplugin")), root, trusted_, true).error.contains(QStringLiteral("plugin id")));
    write(path(QStringLiteral("junk.nfplugin")), "not a zip");
    QVERIFY(!install_plugin(path(QStringLiteral("junk.nfplugin")), root, trusted_, true).error.isEmpty());
    // plugin.json at the top level (no folder) is fine
    archive(QStringLiteral("flat.nfplugin"), {{"plugin.json", manifest}, {"p.py", "pass\n"}});
    r = install_plugin(path(QStringLiteral("flat.nfplugin")), root, trusted_, true);
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QVERIFY(QFileInfo::exists(QDir(root).filePath(QStringLiteral("evil/p.py"))));
  }

  void managerInstallsIntoItsFolder() {
    qputenv("NFCAD_PLUGIN_INSTALL_DIR", path(QStringLiteral("app-plugins")).toLocal8Bit());
    qunsetenv("NFCAD_PLUGIN_PATH");
    qunsetenv("NFCAD_DEVELOPER_MODE");
    QSettings().setValue(QStringLiteral("plugins/folders"), QStringList{path(QStringLiteral("empty"))});
    nfcad::document::Document doc;
    nfcad::document::Selection selection;
    nfcad::results::ResultStore results;
    PluginManager manager(&doc, &selection, &results);
    manager.setTrustedKeysForTesting(trusted_);
    QCOMPARE(manager.folders().front(), path(QStringLiteral("app-plugins")));
    const QString src = path(QStringLiteral("src/gamma"));
    make_plugin(src, QStringLiteral("gamma"), QStringLiteral("2.0"));
    QCOMPARE(sign_plugin(src, key_.seed), QString());
    QCOMPARE(pack_plugin(src, path(QStringLiteral("gamma.nfplugin"))), QString());
    const Installation r = manager.install(path(QStringLiteral("gamma.nfplugin")));
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QVERIFY(manager.plugin(QStringLiteral("gamma")) != nullptr);
    QVERIFY(manager.isInstalled(manager.plugin(QStringLiteral("gamma"))));
    QVERIFY(manager.plugin(QStringLiteral("gamma"))->manifest().trusted);
    QCOMPARE(manager.uninstall(QStringLiteral("gamma")), QString());
    QCOMPARE(manager.plugin(QStringLiteral("gamma")), nullptr);
    qunsetenv("NFCAD_PLUGIN_INSTALL_DIR");
  }

  void environmentCheck() {
    const QString python = find_python();
    if (python.isEmpty()) QSKIP("no Python");
    const EnvironmentReport r = check_environment(
        python, {QStringLiteral("json"), QStringLiteral("nfcad_no_such_package"), QStringLiteral("json>=99"), QStringLiteral("bad req!")});
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QVERIFY(!r.python_version.isEmpty());
    QCOMPARE(r.packages.size(), std::size_t{4});
    QVERIFY(r.packages[0].found);
    QVERIFY(r.packages[0].satisfied);
    QVERIFY(!r.packages[1].found);
    QVERIFY(r.packages[1].error.contains(QStringLiteral("ModuleNotFoundError")));
    QVERIFY(!r.packages[3].satisfied);
    QVERIFY(!r.ok());
    QVERIFY(r.pipCommand().contains(QStringLiteral("nfcad_no_such_package")));
    const EnvironmentReport none = check_environment(path(QStringLiteral("no/python")), {});
    QVERIFY(!none.error.isEmpty());
  }

  void updateInformation() {
    UpdateInfo app = parse_github_release("{\"tag_name\": \"v0.2.0\", \"html_url\": \"https://example.org/r\"}");
    app.installed = QStringLiteral("0.0.1");
    QCOMPARE(app.available, QStringLiteral("0.2.0"));
    QVERIFY(app.newer());
    app.installed = QStringLiteral("0.2.0");
    QVERIFY(!app.newer());
    QVERIFY(!parse_github_release("{}").error.isEmpty());
    const UpdateInfo p = parse_plugin_update("{\"version\": \"1.4\", \"url\": \"https://example.org/p.nfplugin\"}");
    QCOMPARE(p.url, QUrl(QStringLiteral("https://example.org/p.nfplugin")));
  }

  void updateCheckerReadsUrls() {
    // file:// stands in for the web: the checker's request / answer path without a network
    write(path(QStringLiteral("upd/plugin.json")), "{\"version\": \"3.0\", \"url\": \"https://example.org/x\"}");
    write(path(QStringLiteral("upd/release.json")), "{\"tag_name\": \"v9.9.9\", \"html_url\": \"https://example.org/r\"}");
    UpdateChecker checker;
    QSignalSpy finished(&checker, &UpdateChecker::finished);
    checker.check({{QStringLiteral("x"), QStringLiteral("2.0"), QUrl::fromLocalFile(path(QStringLiteral("upd/plugin.json"))), false},
                   {QStringLiteral("app"), QStringLiteral("0.0.1"), QUrl::fromLocalFile(path(QStringLiteral("upd/release.json"))), true},
                   {QStringLiteral("gone"), QStringLiteral("1"), QUrl::fromLocalFile(path(QStringLiteral("upd/missing.json"))), false}});
    QVERIFY(finished.wait(10000));
    const auto results = finished.first().at(0).value<std::vector<UpdateInfo>>();
    QCOMPARE(results.size(), std::size_t{3});
    QVERIFY(results[0].newer());
    QVERIFY(results[1].newer());
    QVERIFY(!results[2].error.isEmpty());
  }
};

QTEST_GUILESS_MAIN(TestPluginDistribution)
#include "test_plugin_distribution.moc"
