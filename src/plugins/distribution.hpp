#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

#include "plugins/plugin_host.hpp"
#include "plugins/signature.hpp"

namespace nfcad::plugins {

/// Distribution of plugins (P7, docs/plugin-api.md "Distribution"): packages, the Python
/// environment and version comparisons for the update check.

// ---------------------------------------------------------------- versions
/// Compares dotted versions numerically ("0.10.0" > "0.9.3"; missing parts count as 0; a
/// suffix such as "-rc1" sorts before the release). -1, 0, 1.
[[nodiscard]] int compare_versions(const QString& a, const QString& b);

/// "name", "name>=1.2" or "name==1.2" → name, operator (empty if none), version.
struct Requirement {
  QString name;
  QString op;
  QString version;
};
[[nodiscard]] std::optional<Requirement> parse_requirement(const QString& text);

// ---------------------------------------------------------------- packages (.nfplugin)
/// A .nfplugin file is a zip archive of one plugin folder (plugin.json at the top or in a
/// single top-level folder), normally with its SIGNATURE.
inline constexpr auto kPackageSuffix = ".nfplugin";

/// Writes the plugin in `dir` (the files of plugin_files() plus SIGNATURE) as a package.
[[nodiscard]] QString pack_plugin(const QString& dir, const QString& package);

struct Installation {
  QString error;           ///< empty on success
  Manifest manifest;       ///< of the installed plugin
  QString directory;       ///< where it was installed
  QString replaced;        ///< version that was replaced (empty: new)
  Verification signature;  ///< result of the signature check
};

/// Unpacks `package` into a temporary folder, checks the manifest and the signature (unsigned
/// only with `allow_unsigned`) and moves it to <plugins_root>/<id>, replacing an installed
/// version. Archive paths must stay inside the folder; sizes are limited.
[[nodiscard]] Installation install_plugin(const QString& package, const QString& plugins_root,
                                          const QStringList& trusted, bool allow_unsigned);

/// Removes <plugins_root>/<id>. Returns an error or empty.
[[nodiscard]] QString uninstall_plugin(const QString& plugins_root, const QString& id);

// ---------------------------------------------------------------- Python environment
struct PackageStatus {
  Requirement requirement;
  bool found = false;
  QString version;  ///< installed version (empty if unknown)
  bool satisfied = false;
  QString error;
};

struct EnvironmentReport {
  QString python;           ///< interpreter that was checked
  QString error;            ///< it could not be run / is too old
  QString python_version;
  std::vector<PackageStatus> packages;
  [[nodiscard]] bool ok() const;
  /// pip command that installs what is missing (empty if nothing).
  [[nodiscard]] QString pipCommand() const;
};

/// Runs `python` (isolated mode) and checks the version (>= 3.10) and the requirements.
/// Blocks for at most `timeout_ms`.
[[nodiscard]] EnvironmentReport check_environment(const QString& python, const QStringList& requirements,
                                                  int timeout_ms = 30000);

}  // namespace nfcad::plugins
