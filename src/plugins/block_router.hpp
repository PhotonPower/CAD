#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <functional>
#include <map>
#include <vector>

#include "plugins/plugin_host.hpp"

namespace nfcad::plugins {

/// Routes the node workflow's requests between plugins (ADR 0010, D2/D3): the engine asks
/// "workflow.blocks" for the block catalogues of the other plugins and "workflow.evaluate" for one
/// block of a provider; the router forwards the request to the provider (started if needed, the
/// request waits for its hello) and its answer back. Payloads are opaque JSON. A provider that
/// stops fails its open requests.
class BlockRouter : public QObject {
  Q_OBJECT
 public:
  /// `hosts`: the plugins; `start`: starts a plugin that is not running.
  BlockRouter(std::function<std::vector<PluginHost*>()> hosts, std::function<void(PluginHost*)> start,
              QObject* parent = nullptr);

  /// Lets `host` ask the router (sets its router) and listens to its answers.
  void attach(PluginHost* host);
  /// {"provider", "version", "type"} per block type of every plugin except `except`.
  [[nodiscard]] QJsonArray catalogues(const PluginHost* except) const;
  /// Requests forwarded and not answered yet.
  [[nodiscard]] std::size_t pending() const { return pending_.size(); }

 private:
  bool route(PluginHost* caller, int rid, const QString& method, const QJsonObject& args);
  void forward(PluginHost* caller, int rid, PluginHost* provider, const QJsonObject& request);
  void answered(PluginHost* provider, int rid, const QJsonObject& response);
  void providerStopped(PluginHost* provider);
  [[nodiscard]] PluginHost* find(const QString& id) const;

  struct Pending {
    QPointer<PluginHost> caller;
    int rid = 0;
    QPointer<PluginHost> provider;
  };
  std::function<std::vector<PluginHost*>()> hosts_;
  std::function<void(PluginHost*)> start_;
  std::map<int, Pending> pending_;  // by the rid sent to the provider
  int next_ = 1;
};

}  // namespace nfcad::plugins
