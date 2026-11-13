#include "plugins/plugin_manager.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include "plugins/signature.hpp"

namespace nfcad::plugins {

PluginManager::PluginManager(document::Document* doc, document::Selection* selection,
                             results::ResultStore* results, QObject* parent)
    : QObject(parent), doc_(doc), selection_(selection), results_(results) {}

PluginManager::~PluginManager() {
  for (auto& h : hosts_) h->stop();
}

QString PluginManager::python() const {
  const QString set = QSettings().value(QStringLiteral("plugins/python")).toString();
  if (!set.isEmpty()) return set;
  const QString env = qEnvironmentVariable("NFCAD_PYTHON");
  if (!env.isEmpty()) return env;
#ifdef NFCAD_PYTHON_SOURCE_DIR
  // development build: the repository's .venv (docs/plugin-api.md)
  for (const char* rel : {"/../.venv/bin/python", "/../.venv/Scripts/python.exe"}) {
    const QString dev = QDir::cleanPath(QStringLiteral(NFCAD_PYTHON_SOURCE_DIR) + QString::fromLatin1(rel));
    if (QFileInfo::exists(dev)) return dev;
  }
#endif
  for (const char* name : {"python3", "python"}) {
    const QString found = QStandardPaths::findExecutable(QString::fromLatin1(name));
    if (!found.isEmpty()) return found;
  }
  return QStringLiteral("python3");
}

void PluginManager::setPython(const QString& path) {
  QSettings().setValue(QStringLiteral("plugins/python"), path);
  for (auto& h : hosts_) h->stop();
}

QString PluginManager::installFolder() {
  const QString env = qEnvironmentVariable("NFCAD_PLUGIN_INSTALL_DIR");
  if (!env.isEmpty()) return env;
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("plugins"));
}

Installation PluginManager::install(const QString& package) {
  for (auto& h : hosts_) h->stop();  // an installed version may be running from its folder
  Installation r = install_plugin(package, installFolder(), trustedKeys(), developerMode());
  reload();
  return r;
}

QString PluginManager::uninstall(const QString& id) {
  for (auto& h : hosts_) h->stop();
  const QString error = uninstall_plugin(installFolder(), id);
  reload();
  return error;
}

bool PluginManager::isInstalled(const PluginHost* host) const {
  return QFileInfo(host->manifest().source_directory).absolutePath() == QDir(installFolder()).absolutePath();
}

EnvironmentReport PluginManager::checkEnvironment() const {
  QStringList requirements{QStringLiteral("numpy")};  // the nfcad package uses it for arrays
  for (const auto& h : hosts_) {
    for (const QString& r : h->manifest().requirements) {
      if (!requirements.contains(r)) requirements << r;
    }
  }
  return check_environment(python(), requirements);
}

QStringList PluginManager::folders() const {
  QStringList out = QSettings().value(QStringLiteral("plugins/folders")).toStringList();
  for (const QString& p : qEnvironmentVariable("NFCAD_PLUGIN_PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
    if (!out.contains(p)) out << p;
  }
#ifdef NFCAD_PYTHON_SOURCE_DIR
  if (out.isEmpty()) {  // development build: the examples and the plugins of the repository
    out << QStringLiteral(NFCAD_PYTHON_SOURCE_DIR "/examples") << QStringLiteral(NFCAD_PYTHON_SOURCE_DIR "/plugins");
  }
#endif
  if (!out.contains(installFolder())) out.prepend(installFolder());  // installed plugins win over same ids
  return out;
}

void PluginManager::setFolders(const QStringList& folders) {
  QSettings().setValue(QStringLiteral("plugins/folders"), folders);
  reload();
}

QString PluginManager::nfcadPythonPath() {
  const QString env = qEnvironmentVariable("NFCAD_PYTHONPATH");
  if (!env.isEmpty()) return env;
  const QDir app(QCoreApplication::applicationDirPath());
  const QStringList candidates = {
      app.filePath(QStringLiteral("../Resources/python")),           // macOS bundle
      app.filePath(QStringLiteral("../share/raytatouille-fusion/python")),  // Linux
      app.filePath(QStringLiteral("../python")),                      // Windows (<prefix>/bin/..)
#ifdef NFCAD_PYTHON_SOURCE_DIR
      QStringLiteral(NFCAD_PYTHON_SOURCE_DIR),                        // development build
#endif
  };
  for (const QString& c : candidates) {
    if (QFileInfo::exists(QDir(c).filePath(QStringLiteral("nfcad/__init__.py")))) return QDir(c).absolutePath();
  }
  return {};
}

bool PluginManager::developerMode() const {
  return QSettings().value(QStringLiteral("plugins/developerMode"), false).toBool() ||
         qEnvironmentVariable("NFCAD_DEVELOPER_MODE") == QLatin1String("1");
}

void PluginManager::setDeveloperMode(bool on) {
  QSettings().setValue(QStringLiteral("plugins/developerMode"), on);
  reload();
}

QStringList PluginManager::trustedKeys() const {
  return trusted_override_ ? *trusted_override_ : built_in_publisher_keys();
}

void PluginManager::reload() {
  for (auto& h : hosts_) h->stop();
  hosts_.clear();
  problems_.clear();
  runtime_ = std::make_unique<QTemporaryDir>();  // the previous copies are removed
  const bool developer = developerMode();
  const QStringList keys = trustedKeys();
  int copies = 0;
  for (const QString& folder : folders()) {
    const QDir dir(folder);
    const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& sub : subdirs) {
      const QString path = dir.filePath(sub);
      if (!QFileInfo::exists(QDir(path).filePath(QStringLiteral("plugin.json")))) continue;
      // signed: check a private copy and run that; otherwise only in developer mode, from the folder
      const QString copy = runtime_->isValid() ? runtime_->filePath(QString::number(++copies)) : QString();
      const Verification v = copy.isEmpty() ? Verification{} : verify_plugin(path, keys, copy);
      if (!v.ok() && !developer) {
        problems_ << tr("%1: %2 – not loaded (only signed plugins; unsigned ones in developer mode)")
                         .arg(QDir::toNativeSeparators(path), v.message.isEmpty() ? tr("cannot be checked") : v.message);
        continue;
      }
      QString error;
      auto m = read_manifest(v.ok() ? copy : path, &error);
      if (!m) {
        problems_ << QStringLiteral("%1: %2").arg(path, error);
        continue;
      }
      m->source_directory = QDir(path).absolutePath();
      m->trusted = v.ok();
      if (!v.ok()) {
        m->trust_note = v.status == Verification::Status::Unsigned ? tr("unsigned") : tr("signature invalid");
        problems_ << tr("%1: %2 – loaded because developer mode is on").arg(QDir::toNativeSeparators(path), v.message);
      }
      if (plugin(m->id) != nullptr) {
        problems_ << tr("%1: plugin id '%2' is used twice").arg(path, m->id);
        continue;
      }
      hosts_.push_back(std::make_unique<PluginHost>(*m, doc_, selection_, results_, this));
    }
  }
  emit pluginsChanged();
}

PluginHost* PluginManager::plugin(const QString& id) const {
  for (const auto& h : hosts_) {
    if (h->manifest().id == id) return h.get();
  }
  return nullptr;
}

int PluginManager::run(PluginHost* host, const QString& action, const QJsonObject& params, const QJsonObject& target) {
  if (!host->running()) host->start(python(), nfcadPythonPath());
  const int job = host->run(action, params, target);
  if (job != 0) emit jobStarted(host, job);
  return job;
}

}  // namespace nfcad::plugins
