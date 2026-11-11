#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <vector>

class QNetworkAccessManager;

namespace nfcad::plugins {

/// Manual update check (P7; Help → Check for updates…). Nothing is checked automatically.
/// Requests carry no personal data: a GET with the user agent "RaytatouilleFusion/<version>".
///
/// - the application: the latest release of the GitHub repository (tag "vX.Y.Z");
/// - plugins with an "update_url" in plugin.json: a JSON document {"version": …, "url": …}.
struct UpdateInfo {
  QString what;        ///< "Raytatouille Fusion" or the plugin name
  QString installed;   ///< current version
  QString available;   ///< newest version (empty if unknown)
  QUrl url;            ///< release page / package download
  QString error;       ///< the check failed
  [[nodiscard]] bool newer() const;
};

inline constexpr auto kReleasesApi = "https://api.github.com/repos/PhotonPower/Raytatouille-Fusion/releases/latest";

/// {"tag_name": "v0.1.0", "html_url": …} → version without "v", page; empty version on errors.
[[nodiscard]] UpdateInfo parse_github_release(const QByteArray& json);
/// {"version": …, "url": …}
[[nodiscard]] UpdateInfo parse_plugin_update(const QByteArray& json);

class UpdateChecker : public QObject {
  Q_OBJECT
 public:
  struct Target {
    QString what;
    QString installed;
    QUrl source;
    bool github = false;  ///< source is the GitHub releases API
  };
  explicit UpdateChecker(QObject* parent = nullptr);
  /// Starts all checks; `finished` once every target answered (or failed / timed out).
  void check(const std::vector<Target>& targets, int timeout_ms = 15000);

 signals:
  void finished(const std::vector<nfcad::plugins::UpdateInfo>& results);

 private:
  QNetworkAccessManager* network_;
};

}  // namespace nfcad::plugins
