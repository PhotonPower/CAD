#include "plugins/plugin_manager.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

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

QStringList PluginManager::folders() const {
  QStringList out = QSettings().value(QStringLiteral("plugins/folders")).toStringList();
  for (const QString& p : qEnvironmentVariable("NFCAD_PLUGIN_PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
    if (!out.contains(p)) out << p;
  }
#ifdef NFCAD_PYTHON_SOURCE_DIR
  if (out.isEmpty()) out << QStringLiteral(NFCAD_PYTHON_SOURCE_DIR "/examples");  // development build
#endif
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

void PluginManager::reload() {
  for (auto& h : hosts_) h->stop();
  hosts_.clear();
  problems_.clear();
  for (const QString& folder : folders()) {
    const QDir dir(folder);
    const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& sub : subdirs) {
      const QString path = dir.filePath(sub);
      if (!QFileInfo::exists(QDir(path).filePath(QStringLiteral("plugin.json")))) continue;
      QString error;
      auto m = read_manifest(path, &error);
      if (!m) {
        problems_ << QStringLiteral("%1: %2").arg(path, error);
        continue;
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

int PluginManager::run(PluginHost* host, const QString& action, const QJsonObject& params) {
  if (!host->running()) host->start(python(), nfcadPythonPath());
  const int job = host->run(action, params);
  if (job != 0) emit jobStarted(host, job);
  return job;
}

}  // namespace nfcad::plugins
