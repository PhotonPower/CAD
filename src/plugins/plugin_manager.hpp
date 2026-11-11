#pragma once

#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <memory>
#include <optional>
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
/// - Signatures (P5, decision D3): only plugins signed with a built-in publisher key are loaded;
///   they run from a private copy of exactly the signed files. Developer mode (setting
///   "plugins/developerMode" or $NFCAD_DEVELOPER_MODE=1) also loads unsigned plugins, marked.
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

  [[nodiscard]] bool developerMode() const;
  /// Saved in the settings; reloads the plugins.
  void setDeveloperMode(bool on);
  /// Publisher keys whose signatures are accepted (built in; tests may replace them).
  [[nodiscard]] QStringList trustedKeys() const;
  void setTrustedKeysForTesting(const QStringList& keys) { trusted_override_ = keys; }

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
  std::unique_ptr<QTemporaryDir> runtime_;  ///< checked copies of the signed plugins
  std::optional<QStringList> trusted_override_;
};

}  // namespace nfcad::plugins
