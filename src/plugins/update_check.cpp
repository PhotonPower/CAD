#include "plugins/update_check.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <memory>

#include "plugins/distribution.hpp"

namespace nfcad::plugins {

bool UpdateInfo::newer() const {
  return error.isEmpty() && !available.isEmpty() && compare_versions(available, installed) > 0;
}

UpdateInfo parse_github_release(const QByteArray& json) {
  UpdateInfo u;
  const QJsonObject o = QJsonDocument::fromJson(json).object();
  QString tag = o["tag_name"].toString();
  if (tag.startsWith(QLatin1Char('v'))) tag.remove(0, 1);
  u.available = tag;
  u.url = QUrl(o["html_url"].toString());
  if (tag.isEmpty()) u.error = QObject::tr("no release found");
  return u;
}

UpdateInfo parse_plugin_update(const QByteArray& json) {
  UpdateInfo u;
  const QJsonObject o = QJsonDocument::fromJson(json).object();
  u.available = o["version"].toString();
  u.url = QUrl(o["url"].toString());
  if (u.available.isEmpty()) u.error = QObject::tr("no version in the update information");
  return u;
}

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {}

void UpdateChecker::check(const std::vector<Target>& targets, int timeout_ms) {
  auto results = std::make_shared<std::vector<UpdateInfo>>(targets.size());
  auto pending = std::make_shared<int>(static_cast<int>(targets.size()));
  if (targets.empty()) {
    emit finished({});
    return;
  }
  for (std::size_t i = 0; i < targets.size(); ++i) {
    const Target t = targets[i];
    QNetworkRequest request(t.source);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("RaytatouilleFusion/%1").arg(QStringLiteral(NFCAD_VERSION)));
    if (t.github) request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(timeout_ms);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, t, i, results, pending] {
      UpdateInfo u;
      if (reply->error() != QNetworkReply::NoError) {
        u.error = reply->errorString();
      } else {
        const QByteArray body = reply->read(1024 * 1024);
        u = t.github ? parse_github_release(body) : parse_plugin_update(body);
      }
      u.what = t.what;
      u.installed = t.installed;
      (*results)[i] = u;
      reply->deleteLater();
      if (--*pending == 0) emit finished(*results);
    });
  }
}

}  // namespace nfcad::plugins
