#pragma once

#include <QObject>
#include <QStringList>
#include <memory>
#include <vector>

#include "plugins/plugin_host.hpp"

namespace nfcad::plugins {

/// Finds plugins and the Python environment and owns one PluginHost per plugin
/// (docs/plugin-api.md, "Installation").
///
/// - Python: setting "plugins/python", else $NFCAD_PYTHON, else python3 / python on the PATH
///   (decision D5: a user-selected environment with the kernel libraries).
/// - Plugin folders: setting "plugins/folders", plus $NFCAD_PLUGIN_PATH; every sub-directory with
///   a plugin.json is a plugin.
/// - The nfcad package: $NFCAD_PYTHONPATH, else next to the application (installed), else the
///   source tree (development build).
class PluginManager : public QObject {
  Q_OBJECT
 public:
  PluginManager(document::Document* doc, document::Selection* selection,
                results::ResultStore* results, QObject* parent = nullptr);
  ~PluginManager() override;

  [[nodiscard]] QString python() const;
  void setPython(const QString& path);
  [[nodiscard]] QStringList folders() const;
  void setFolders(const QStringList& folders);
  [[nodiscard]] static QString nfcadPythonPath();

  /// Re-reads the plugin folders (stops running plugins).
  void reload();
  [[nodiscard]] const std::vector<std::unique_ptr<PluginHost>>& plugins() const { return hosts_; }
  [[nodiscard]] PluginHost* plugin(const QString& id) const;
  /// Problems found while loading (invalid manifests, …).
  [[nodiscard]] const QStringList& problems() const { return problems_; }

  /// Starts the plugin if needed and runs the action.
  int run(PluginHost* host, const QString& action, const QJsonObject& params = {});

 signals:
  void pluginsChanged();
  void jobStarted(nfcad::plugins::PluginHost* host, int job);

 private:
  document::Document* doc_;
  document::Selection* selection_;
  results::ResultStore* results_;
  std::vector<std::unique_ptr<PluginHost>> hosts_;
  QStringList problems_;
};

}  // namespace nfcad::plugins
