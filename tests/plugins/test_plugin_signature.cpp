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

#include "document/document.hpp"
#include "document/selection.hpp"
#include "plugins/plugin_manager.hpp"
#include "plugins/signature.hpp"
#include "results/result_store.hpp"

using namespace nfcad::plugins;

namespace {
bool copy_tree(const QString& from, const QString& to) {
  QDir().mkpath(to);
  for (const QString& rel : plugin_files(from)) {
    QDir().mkpath(QFileInfo(QDir(to).filePath(rel)).absolutePath());
    if (!QFile::copy(QDir(from).filePath(rel), QDir(to).filePath(rel))) return false;
  }
  return true;
}

void append(const QString& path, const QByteArray& text) {
  QFile f(path);
  QVERIFY(f.open(QIODevice::Append));
  f.write(text);
}

QString find_python() {
  const QString env = qEnvironmentVariable("NFCAD_TEST_PYTHON");
  if (!env.isEmpty()) return env;
  for (const char* n : {"python3", "python"}) {
    const QString p = QStandardPaths::findExecutable(QString::fromLatin1(n));
    if (!p.isEmpty()) return p;
  }
  return {};
}
}  // namespace

class TestPluginSignature : public QObject {
  Q_OBJECT
  QTemporaryDir tmp_;
  KeyPair key_;
  KeyPair other_;
  QString plugins_;  ///< folder with plugins

  QString fresh(const QString& name) {
    const QString dir = QDir(plugins_).filePath(name);
    QDir(dir).removeRecursively();
    copy_tree(QStringLiteral(NFCAD_TEST_PLUGIN_DIR), dir);
    return dir;
  }

 private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("PhotonPower-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_plugin_signature"));
    QSettings().clear();
    qunsetenv("NFCAD_DEVELOPER_MODE");
    qunsetenv("NFCAD_PLUGIN_PATH");
    key_ = generate_key_pair();
    other_ = generate_key_pair();
    QCOMPARE(key_.public_key.size(), 32);
    QVERIFY(key_.public_key != other_.public_key);
    plugins_ = tmp_.filePath(QStringLiteral("plugins"));
  }
  void cleanupTestCase() { QSettings().clear(); }

  void rfc8032TestVector() {
    // RFC 8032, 7.1 TEST 1: secret key -> public key (signature checked through sign / verify)
    const QByteArray seed = QByteArray::fromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    QCOMPARE(public_key_from_seed(seed).toHex(),
             QByteArray("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
  }

  void keyFormats() {
    const QString line = format_public_key(key_.public_key) + QStringLiteral("  publisher comment");
    QCOMPARE(parse_public_key(line), key_.public_key);
    QVERIFY(parse_public_key(QStringLiteral("ed25519:abc")).isEmpty());
    QVERIFY(parse_public_key(QStringLiteral("rsa:") + QString::fromLatin1(key_.public_key.toBase64())).isEmpty());
    const QString file = tmp_.filePath(QStringLiteral("k/secret.key"));
    QVERIFY(write_secret_key(file, key_.seed));
    QCOMPARE(read_secret_key(file), key_.seed);
    QVERIFY(!write_secret_key(file, other_.seed));  // never overwritten
    QCOMPARE(read_secret_key(file), key_.seed);
#ifndef Q_OS_WIN
    QCOMPARE(QFile::permissions(file) & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions{});
#endif
  }

  void signAndVerify() {
    const QString dir = fresh(QStringLiteral("p"));
    const QStringList trusted{format_public_key(key_.public_key)};
    QCOMPARE(verify_plugin(dir, trusted).status, Verification::Status::Unsigned);
    QCOMPARE(sign_plugin(dir, key_.seed), QString());
    const Verification v = verify_plugin(dir, trusted);
    QVERIFY2(v.ok(), qPrintable(v.message));
    QVERIFY(v.files.contains(QStringLiteral("plugin.json")));
    QVERIFY(!v.files.contains(QStringLiteral("SIGNATURE")));
    // other key
    QCOMPARE(verify_plugin(dir, {format_public_key(other_.public_key)}).status, Verification::Status::UntrustedKey);
    QCOMPARE(verify_plugin(dir, {}).status, Verification::Status::UntrustedKey);
    // modified file
    append(QDir(dir).filePath(QStringLiteral("test_plugin.py")), "\n# changed\n");
    const Verification m = verify_plugin(dir, trusted);
    QCOMPARE(m.status, Verification::Status::Invalid);
    QVERIFY(m.message.contains(QStringLiteral("test_plugin.py")));
  }

  void tamperedSignatureFile() {
    const QString dir = fresh(QStringLiteral("t"));
    QCOMPARE(sign_plugin(dir, key_.seed), QString());
    const QString sig_path = QDir(dir).filePath(QStringLiteral("SIGNATURE"));
    QFile f(sig_path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonObject sig = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    auto write = [&](const QJsonObject& o) {
      QFile out(sig_path);
      QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
      out.write(QJsonDocument(o).toJson());
    };
    const QStringList trusted{format_public_key(key_.public_key)};
    // a file dropped from the list: the signed message differs
    QJsonObject fewer = sig;
    QJsonObject files = fewer["files"].toObject();
    files.remove(QStringLiteral("test_plugin.py"));
    fewer["files"] = files;
    write(fewer);
    QCOMPARE(verify_plugin(dir, trusted).status, Verification::Status::Invalid);
    // a path leaving the folder
    QJsonObject escape = sig;
    files = escape["files"].toObject();
    files.insert(QStringLiteral("../outside.py"), QStringLiteral("00"));
    escape["files"] = files;
    write(escape);
    QVERIFY(verify_plugin(dir, trusted).message.contains(QStringLiteral("invalid path")));
    // the key swapped for an attacker's: not trusted
    QJsonObject swapped = sig;
    swapped["key"] = format_public_key(other_.public_key);
    write(swapped);
    QCOMPARE(verify_plugin(dir, trusted).status, Verification::Status::UntrustedKey);
    write(sig);
    QVERIFY(verify_plugin(dir, trusted).ok());
  }

  void copyHoldsOnlySignedFiles() {
    const QString dir = fresh(QStringLiteral("c"));
    QCOMPARE(sign_plugin(dir, key_.seed), QString());
    append(QDir(dir).filePath(QStringLiteral("extra.py")), "import os\n");  // added after signing
    QTemporaryDir copy;
    const Verification v = verify_plugin(dir, {format_public_key(key_.public_key)}, copy.path());
    QVERIFY2(v.ok(), qPrintable(v.message));
    QVERIFY(QFileInfo::exists(copy.filePath(QStringLiteral("plugin.json"))));
    QVERIFY(!QFileInfo::exists(copy.filePath(QStringLiteral("extra.py"))));
  }

  void managerLoadsOnlySignedPlugins() {
    QDir(plugins_).removeRecursively();
    const QString good = fresh(QStringLiteral("a_signed"));
    QCOMPARE(sign_plugin(good, key_.seed), QString());
    const QString unsigned_dir = fresh(QStringLiteral("b_unsigned"));
    // same plugin id twice would be rejected: give the unsigned one its own id
    QFile mf(QDir(unsigned_dir).filePath(QStringLiteral("plugin.json")));
    QVERIFY(mf.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
    mf.close();
    manifest["id"] = QStringLiteral("test_unsigned");
    QVERIFY(mf.open(QIODevice::WriteOnly | QIODevice::Truncate));
    mf.write(QJsonDocument(manifest).toJson());
    mf.close();

    nfcad::document::Document doc;
    nfcad::document::Selection selection;
    nfcad::results::ResultStore results;
    PluginManager manager(&doc, &selection, &results);
    manager.setTrustedKeysForTesting({format_public_key(key_.public_key)});
    QSettings().setValue(QStringLiteral("plugins/developerMode"), false);
    manager.setFolders({plugins_});  // reloads
    QCOMPARE(manager.plugins().size(), std::size_t{1});
    const Manifest& m = manager.plugins().front()->manifest();
    QVERIFY(m.trusted);
    QCOMPARE(m.displayName(), m.name);
    QCOMPARE(m.source_directory, QDir(good).absolutePath());
    QVERIFY(m.directory != m.source_directory);  // runs from the checked copy
    QVERIFY(manager.problems().join(QLatin1Char('\n')).contains(QStringLiteral("b_unsigned")));

    // changing the installed plugin afterwards does not reach the copy
    append(QDir(good).filePath(QStringLiteral("test_plugin.py")), "\nraise SystemExit('tampered')\n");
    QFile copy(QDir(m.directory).filePath(QStringLiteral("test_plugin.py")));
    QVERIFY(copy.open(QIODevice::ReadOnly));
    QVERIFY(!copy.readAll().contains("tampered"));

    // the signed copy really starts
    if (const QString python = find_python(); !python.isEmpty()) {
      PluginHost* host = manager.plugins().front().get();
      QSignalSpy ready(host, &PluginHost::readyChanged);
      host->start(python, PluginManager::nfcadPythonPath());
      QVERIFY(ready.wait(20000));
      QVERIFY(host->ready());
      host->stop();
    }

    // now tampered: rejected after a reload
    manager.reload();
    QCOMPARE(manager.plugins().size(), std::size_t{0});

    // developer mode: both load, marked
    manager.setDeveloperMode(true);
    QCOMPARE(manager.plugins().size(), std::size_t{2});
    for (const auto& h : manager.plugins()) {
      QVERIFY(!h->manifest().trusted);
      QVERIFY(h->manifest().displayName().contains(QLatin1Char('[')));
    }
    manager.setDeveloperMode(false);
  }
};

QTEST_GUILESS_MAIN(TestPluginSignature)
#include "test_plugin_signature.moc"
