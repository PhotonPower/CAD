#include "plugins/block_router.hpp"

#include <memory>
#include <utility>

namespace nfcad::plugins {

BlockRouter::BlockRouter(std::function<std::vector<PluginHost*>()> hosts, std::function<void(PluginHost*)> start,
                         QObject* parent)
    : QObject(parent), hosts_(std::move(hosts)), start_(std::move(start)) {}

void BlockRouter::attach(PluginHost* host) {
  host->setRouter([this](PluginHost* self, int rid, const QString& method, const QJsonObject& args) {
    return route(self, rid, method, args);
  });
  connect(host, &PluginHost::evaluated, this,
          [this, host](int rid, const QJsonObject& response) { answered(host, rid, response); });
  connect(host, &PluginHost::stopped, this, [this, host] { providerStopped(host); });
}

PluginHost* BlockRouter::find(const QString& id) const {
  for (PluginHost* h : hosts_()) {
    if (h->manifest().id == id) return h;
  }
  return nullptr;
}

QJsonArray BlockRouter::catalogues(const PluginHost* except) const {
  QJsonArray out;
  for (PluginHost* h : hosts_()) {
    if (h == except) continue;
    for (const QJsonValue& b : h->blocks()) {
      out.append(QJsonObject{{"provider", h->manifest().id}, {"version", h->manifest().version}, {"type", b}});
    }
  }
  return out;
}

bool BlockRouter::route(PluginHost* caller, int rid, const QString& method, const QJsonObject& args) {
  if (method == QLatin1String("workflow.blocks")) {
    // a plugin tells its blocks in its hello: start the ones not running and answer when every
    // plugin is ready or failed
    std::vector<PluginHost*> waiting;
    for (PluginHost* h : hosts_()) {
      if (h != caller && !h->ready()) waiting.push_back(h);
    }
    if (waiting.empty()) {
      caller->respond(rid, true, catalogues(caller));
      return true;
    }
    auto* wait = new QObject(this);
    auto left = std::make_shared<std::size_t>(waiting.size());
    auto done = [this, wait, left, guard = QPointer<PluginHost>(caller), rid] {
      if (--*left > 0) return;
      wait->deleteLater();
      if (guard) guard->respond(rid, true, catalogues(guard));
    };
    for (PluginHost* h : waiting) {
      auto once = std::make_shared<bool>(false);
      auto finish = [done, once] {
        if (*once) return;
        *once = true;
        done();
      };
      connect(h, &PluginHost::readyChanged, wait, [finish](bool ready) {
        if (ready) finish();
      });
      connect(h, &PluginHost::failed, wait, [finish](const QString&) { finish(); });
      if (!h->running() && start_) start_(h);
    }
    return true;
  }
  if (method != QLatin1String("workflow.evaluate")) return false;
  PluginHost* provider = find(args["provider"].toString());
  if (provider == nullptr || provider == caller) {
    caller->respond(rid, false, {}, tr("no plugin %1 provides blocks").arg(args["provider"].toString()));
    return true;
  }
  const QJsonObject request = args["request"].toObject();
  if (provider->ready()) {
    forward(caller, rid, provider, request);
    return true;
  }
  // not running or not ready yet: start it and send the request after its hello
  auto* wait = new QObject(this);
  connect(provider, &PluginHost::readyChanged, wait,
          [this, wait, guard = QPointer<PluginHost>(caller), rid, provider, request](bool ready) {
            if (!ready) return;
            wait->deleteLater();
            if (guard) forward(guard, rid, provider, request);
          });
  connect(provider, &PluginHost::failed, wait, [wait, guard = QPointer<PluginHost>(caller), rid](const QString& why) {
    wait->deleteLater();
    if (guard) guard->respond(rid, false, {}, why);
  });
  if (!provider->running() && start_) start_(provider);
  return true;
}

void BlockRouter::forward(PluginHost* caller, int rid, PluginHost* provider, const QJsonObject& request) {
  const int sent = next_++;
  pending_[sent] = Pending{caller, rid, provider};
  provider->evaluate(sent, request);
}

void BlockRouter::answered(PluginHost* provider, int rid, const QJsonObject& response) {
  const auto it = pending_.find(rid);
  if (it == pending_.end() || it->second.provider != provider) return;
  const Pending p = it->second;
  pending_.erase(it);
  if (p.caller) p.caller->respond(p.rid, true, response);
}

void BlockRouter::providerStopped(PluginHost* provider) {
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (it->second.provider == provider) {
      if (it->second.caller) {
        it->second.caller->respond(it->second.rid, false, {},
                                   tr("the plugin %1 stopped while evaluating a block").arg(provider->manifest().id));
      }
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace nfcad::plugins
