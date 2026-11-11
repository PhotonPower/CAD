#include "plugins/plugin_host.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUuid>
#include <cstring>
#include <stdexcept>

#include "document/nfcad_io.hpp"
#include "document/selection.hpp"
#include "results/result_store.hpp"

namespace nfcad::plugins {

namespace {

/// Reads an array file written by nfcad.arrays.write (little endian) and deletes it. Only files
/// directly inside `dir` (the host's private directory, NFCAD_ARRAY_DIR) are accepted: a faulty
/// plugin must not make CAD read or delete other files.
template <class T>
std::vector<T> read_array(const QJsonValue& descriptor, const QString& dir) {
  const QJsonObject d = descriptor.toObject();
  const QString path = d["file"].toString();
  const auto count = static_cast<std::size_t>(d["count"].toInteger());
  std::vector<T> out;
  if (path.isEmpty()) return out;
  const QFileInfo info(path);
  // the plugin builds the path from NFCAD_ARRAY_DIR, so the cleaned directory strings agree;
  // the canonical paths are compared as a fallback (symbolic links, 8.3 names on Windows)
  const QString parent = QDir::cleanPath(QDir::fromNativeSeparators(info.absolutePath()));
  const bool inside = parent == QDir::cleanPath(QDir::fromNativeSeparators(dir)) ||
                      QFileInfo(info.absolutePath()).canonicalFilePath() == QFileInfo(dir).canonicalFilePath();
  if (dir.isEmpty() || !info.fileName().startsWith(QLatin1String("nfcad-")) ||
      !info.fileName().endsWith(QLatin1String(".bin")) || !inside) {
    throw std::runtime_error("array file is not in the plugin's array directory");
  }
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot open array file " + path.toStdString());
  if (static_cast<std::size_t>(f.size()) < count * sizeof(T)) {
    throw std::runtime_error("array file too short");
  }
  out.resize(count);
  if (count > 0) {
    uchar* mem = f.map(0, static_cast<qint64>(count * sizeof(T)));
    if (mem != nullptr) {
      std::memcpy(out.data(), mem, count * sizeof(T));
      f.unmap(mem);
    } else {
      f.read(reinterpret_cast<char*>(out.data()), static_cast<qint64>(count * sizeof(T)));
    }
  }
  f.close();
  f.remove();
  return out;
}

QJsonObject entity_json(const document::Entity& e) {
  return {{"id", static_cast<qint64>(e.id)},
          {"name", e.name},
          {"group", static_cast<int>(e.group)},
          {"attributes", QJsonObject::fromVariantMap(e.attributes)},
          {"geometry", document::geometry_to_json(*e.geometry)}};
}

std::optional<results::Range> range_of(const QJsonObject& args) {
  const QJsonArray r = args["range"].toArray();
  if (r.size() != 2) return std::nullopt;
  return results::Range{r[0].toDouble(), r[1].toDouble()};
}

}  // namespace

std::optional<Manifest> read_manifest(const QString& dir, QString* error) {
  auto fail = [&](const QString& m) -> std::optional<Manifest> {
    if (error != nullptr) *error = m;
    return std::nullopt;
  };
  QFile f(QDir(dir).filePath(QStringLiteral("plugin.json")));
  if (!f.open(QIODevice::ReadOnly)) return fail(QObject::tr("no plugin.json in %1").arg(dir));
  QJsonParseError pe;
  const QJsonObject o = QJsonDocument::fromJson(f.readAll(), &pe).object();
  if (pe.error != QJsonParseError::NoError) return fail(pe.errorString());
  Manifest m;
  m.id = o["id"].toString();
  m.name = o["name"].toString(m.id);
  m.version = o["version"].toString();
  m.api = o["api"].toInt();
  m.entry = o["entry"].toString();
  m.description = o["description"].toString();
  m.directory = QDir(dir).absolutePath();
  if (m.id.isEmpty() || m.entry.isEmpty()) return fail(QObject::tr("plugin.json needs 'id' and 'entry'"));
  if (m.api != kPluginApiVersion) {
    return fail(QObject::tr("plugin API %1 is not supported (this version: %2)").arg(m.api).arg(kPluginApiVersion));
  }
  return m;
}

PluginHost::PluginHost(Manifest manifest, document::Document* doc, document::Selection* selection,
                       results::ResultStore* results, QObject* parent)
    : QObject(parent), manifest_(std::move(manifest)), doc_(doc), selection_(selection), results_(results) {}

PluginHost::~PluginHost() { stop(); }

QString PluginHost::arrayDir() const { return array_dir_ && array_dir_->isValid() ? array_dir_->path() : QString(); }

quint16 PluginHost::channelPort() const { return server_ != nullptr ? server_->serverPort() : 0; }

bool PluginHost::running() const { return process_ != nullptr && process_->state() != QProcess::NotRunning; }

void PluginHost::start(const QString& python, const QString& pythonpath) {
  if (running()) return;
  ready_ = false;
  stderr_.clear();
  // TCP on the loopback interface (full duplex on every platform); the token authenticates
  // the plugin process
  server_ = new QTcpServer(this);
  if (!server_->listen(QHostAddress::LocalHost, 0)) {
    emit failed(tr("cannot open the plugin channel: %1").arg(server_->errorString()));
    return;
  }
  connect(server_, &QTcpServer::newConnection, this, &PluginHost::onConnection);
  token_ = QUuid::createUuid().toByteArray(QUuid::Id128) + QByteArray::number(QRandomGenerator::system()->generate64(), 16);
  authenticated_ = false;

  process_ = new QProcess(this);
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const QString sep = QDir::listSeparator();
  env.insert(QStringLiteral("PYTHONPATH"),
             pythonpath + (env.contains(QStringLiteral("PYTHONPATH")) ? sep + env.value(QStringLiteral("PYTHONPATH")) : QString()));
  env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
  env.insert(QStringLiteral("NFCAD_TOKEN"), QString::fromLatin1(token_));  // not on the command line
  array_dir_ = std::make_unique<QTemporaryDir>(QDir::temp().filePath(QStringLiteral("nfcad-arrays-XXXXXX")));
  env.insert(QStringLiteral("NFCAD_ARRAY_DIR"), QDir::toNativeSeparators(array_dir_->path()));
  process_->setProcessEnvironment(env);
  process_->setWorkingDirectory(manifest_.directory);
  connect(process_, &QProcess::readyReadStandardError, this, [this] {
    const QString text = QString::fromUtf8(process_->readAllStandardError());
    stderr_ += text;
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) emit logLine(line);
  });
  connect(process_, &QProcess::readyReadStandardOutput, this, [this] {
    for (const QString& line : QString::fromUtf8(process_->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
      emit logLine(line);
    }
  });
  connect(process_, &QProcess::finished, this, &PluginHost::onProcessFinished);
  connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
    if (e == QProcess::FailedToStart) {
      emit failed(tr("cannot start Python (%1): %2").arg(process_->program(), process_->errorString()));
    }
  });
  process_->start(python, {QStringLiteral("-m"), QStringLiteral("nfcad.host"), QStringLiteral("--server"),
                           QStringLiteral("127.0.0.1:%1").arg(server_->serverPort()), QStringLiteral("--plugin"),
                           manifest_.directory});
}

void PluginHost::onConnection() {
  QTcpSocket* s = server_->nextPendingConnection();
  if (s == nullptr || socket_ != nullptr) {
    if (s != nullptr) s->abort();
    return;
  }
  socket_ = s;
  connect(s, &QTcpSocket::readyRead, this, &PluginHost::onReadyRead);
}

void PluginHost::onReadyRead() {
  buffer_ += socket_->readAll();
  qsizetype nl;
  while ((nl = buffer_.indexOf('\n')) >= 0) {
    const QByteArray line = buffer_.left(nl);
    buffer_.remove(0, nl + 1);
    QJsonParseError pe;
    const QJsonObject msg = QJsonDocument::fromJson(line, &pe).object();
    if (!authenticated_) {
      // the first line must carry the token of the process CAD started
      if (pe.error == QJsonParseError::NoError && msg["type"].toString() == QLatin1String("auth") &&
          msg["token"].toString().toLatin1() == token_) {
        authenticated_ = true;
        continue;
      }
      emit logLine(tr("rejected a connection without the correct token"));
      socket_->abort();
      socket_ = nullptr;
      buffer_.clear();
      return;
    }
    if (pe.error == QJsonParseError::NoError) handle(msg);
  }
}

void PluginHost::send(const QJsonObject& msg) {
  if (socket_ == nullptr) return;
  socket_->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
  socket_->flush();
}

void PluginHost::reply(int rid, bool ok, const QJsonValue& result, const QString& error) {
  QJsonObject r{{"type", "reply"}, {"rid", rid}, {"ok", ok}};
  if (ok) {
    r["result"] = result;
  } else {
    r["error"] = error;
  }
  send(r);
}

void PluginHost::handle(const QJsonObject& msg) {
  const QString type = msg["type"].toString();
  if (type == QLatin1String("hello")) {
    actions_.clear();
    for (const QJsonValue& a : msg["actions"].toArray()) {
      const QJsonObject o = a.toObject();
      actions_.push_back({o["id"].toString(), o["label"].toString(), o["description"].toString()});
    }
    params_ = msg["params"].toObject();
    ready_ = true;
    emit readyChanged(true);
    if (queued_) {
      const auto [action, params] = *queued_;
      queued_.reset();
      run(action, params);
    }
  } else if (type == QLatin1String("progress")) {
    emit progress(msg["job"].toInt(), msg["value"].toDouble(), msg["text"].toString());
  } else if (type == QLatin1String("log")) {
    for (const QString& line : msg["text"].toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) emit logLine(line);
  } else if (type == QLatin1String("done")) {
    endJob(msg["job"].toInt(), msg["ok"].toBool(), msg["cancelled"].toBool(), msg["message"].toString());
  } else if (type == QLatin1String("request") || type == QLatin1String("notify")) {
    const int rid = msg["rid"].toInt();
    try {
      const QJsonValue result = request(msg["method"].toString(), msg["args"].toObject());
      if (type == QLatin1String("request")) reply(rid, true, result);
    } catch (const std::exception& e) {
      if (type == QLatin1String("request")) {
        reply(rid, false, {}, QString::fromUtf8(e.what()));
      } else {
        emit logLine(tr("notification failed: %1").arg(QString::fromUtf8(e.what())));
      }
    }
  }
}

QJsonValue PluginHost::request(const QString& method, const QJsonObject& args) {
  using document::EntityId;
  if (method == QLatin1String("model.entities")) {
    QJsonArray out;
    std::vector<EntityId> ids;
    if (args["selection"].toBool() && selection_ != nullptr) {
      ids = selection_->entities();
    } else if (args.contains("ids")) {
      for (const QJsonValue& v : args["ids"].toArray()) ids.push_back(static_cast<EntityId>(v.toInteger()));
    } else {
      for (const auto& [id, e] : doc_->entities()) ids.push_back(id);
    }
    for (EntityId id : ids) {
      if (const document::Entity* e = doc_->entity(id)) out.append(entity_json(*e));
    }
    return out;
  }
  if (method == QLatin1String("model.add")) {
    document::Entity e = document::make_entity(document::geometry_from_json(args["geometry"].toObject()),
                                               args["name"].toString());
    e.attributes = args["attributes"].toObject().toVariantMap();
    const EntityId id = doc_->insert(std::move(e));  // live; one undo step at the end of the job
    added_.push_back(id);
    return static_cast<qint64>(id);
  }
  if (method == QLatin1String("model.replace")) {
    const auto id = static_cast<EntityId>(args["id"].toInteger());
    const document::Entity* e = doc_->entity(id);
    if (e == nullptr) throw std::runtime_error("unknown entity");
    auto g = std::make_shared<const document::Geometry>(document::geometry_from_json(args["geometry"].toObject()));
    const document::GeometryPtr old = doc_->setGeometry(id, std::move(g));
    original_geometry_.try_emplace(id, old);  // keep the state before the job
    return QJsonValue::Null;
  }
  if (method == QLatin1String("model.set_attribute")) {
    const auto id = static_cast<EntityId>(args["id"].toInteger());
    const document::Entity* e = doc_->entity(id);
    if (e == nullptr) throw std::runtime_error("unknown entity");
    original_attributes_.try_emplace(id, e->attributes);
    QVariantMap a = e->attributes;
    if (args["value"].isNull()) {
      a.remove(args["key"].toString());
    } else {
      a.insert(args["key"].toString(), args["value"].toVariant());
    }
    doc_->setEntityAttributes(id, a);
    return QJsonValue::Null;
  }
  if (method == QLatin1String("results.create_set")) {
    return static_cast<qint64>(results_->createSet(args["name"].toString(), manifest_.id));
  }
  if (method == QLatin1String("results.lines")) {
    results::LineSet l;
    l.points = read_array<float>(args["points"], arrayDir());
    l.starts = read_array<std::uint32_t>(args["starts"], arrayDir());
    l.values = read_array<float>(args["values"], arrayDir());
    if (l.starts.empty() && !l.points.empty()) l.starts = {0, static_cast<std::uint32_t>(l.pointCount())};
    if (l.starts.back() > l.pointCount()) throw std::runtime_error("polyline starts beyond the points");
    l.colormap = results::colormap_from_name(args["colormap"].toString());
    l.unit = args["unit"].toString();
    l.on_top = args["on_top"].toBool(true);
    l.range = range_of(args);
    return static_cast<qint64>(results_->addLines(static_cast<results::ResultId>(args["set"].toInteger()),
                                                  args["name"].toString(), std::move(l)));
  }
  if (method == QLatin1String("results.field")) {
    results::FaceField f;
    f.entity = static_cast<EntityId>(args["entity"].toInteger());
    f.face = args["face"].toInt();
    f.width = args["width"].toInt();
    f.height = args["height"].toInt();
    f.values = read_array<float>(args["values"], arrayDir());
    if (f.width <= 0 || f.height <= 0 || f.values.size() != static_cast<std::size_t>(f.width) * static_cast<std::size_t>(f.height)) {
      throw std::runtime_error("field: values do not match width x height");
    }
    f.colormap = results::colormap_from_name(args["colormap"].toString());
    f.unit = args["unit"].toString();
    f.range = range_of(args);
    return static_cast<qint64>(results_->addField(static_cast<results::ResultId>(args["set"].toInteger()),
                                                  args["name"].toString(), std::move(f)));
  }
  if (method == QLatin1String("results.curve")) {
    results::Curve c;
    for (const QJsonValue& v : args["x"].toArray()) c.x.push_back(v.toDouble());
    for (const QJsonValue& v : args["y"].toArray()) c.y.push_back(v.toDouble());
    c.x_label = args["x_label"].toString();
    c.y_label = args["y_label"].toString();
    return static_cast<qint64>(results_->addCurve(static_cast<results::ResultId>(args["set"].toInteger()),
                                                  args["name"].toString(), std::move(c)));
  }
  if (method == QLatin1String("results.append")) {
    results_->appendToCurve(static_cast<results::ResultId>(args["item"].toInteger()), args["x"].toDouble(),
                            args["y"].toDouble());
    return QJsonValue::Null;
  }
  throw std::runtime_error("unknown method " + method.toStdString());
}

int PluginHost::run(const QString& action, const QJsonObject& params) {
  if (active_job_) return 0;
  if (!ready_) {
    queued_ = std::make_pair(action, params);  // sent when the plugin said hello
    return next_job_;
  }
  const int job = next_job_++;
  beginJob(job);
  QJsonArray selection;
  if (selection_ != nullptr) {
    for (document::EntityId id : selection_->entities()) selection.append(static_cast<qint64>(id));
  }
  send({{"type", "run"}, {"job", job}, {"action", action}, {"params", params}, {"selection", selection}});
  return job;
}

void PluginHost::beginJob(int job) {
  active_job_ = job;
  original_geometry_.clear();
  original_attributes_.clear();
  added_.clear();
}

void PluginHost::endJob(int job, bool ok, bool cancelled, const QString& message) {
  if (!active_job_ || *active_job_ != job) return;
  active_job_.reset();
  // everything the run changed in the model is one undo step (also when cancelled)
  std::vector<std::pair<document::EntityId, document::GeometryPtr>> geometry(original_geometry_.begin(),
                                                                              original_geometry_.end());
  std::vector<std::pair<document::EntityId, QVariantMap>> attributes(original_attributes_.begin(),
                                                                     original_attributes_.end());
  doc_->recordApplied(manifest_.name, added_, geometry, attributes);
  original_geometry_.clear();
  original_attributes_.clear();
  added_.clear();
  emit finished(job, ok, cancelled, message);
}

void PluginHost::cancel(int grace_ms) {
  if (!active_job_) return;
  const int job = *active_job_;
  send({{"type", "cancel"}, {"job", job}});
  QTimer::singleShot(grace_ms, this, [this, job] {
    if (active_job_ && *active_job_ == job && running()) {
      emit logLine(tr("the plugin did not stop – terminating its process"));
      process_->kill();
    }
  });
}

void PluginHost::stop() {
  if (process_ != nullptr && running()) {
    send({{"type", "shutdown"}});
    if (!process_->waitForFinished(2000)) {
      process_->kill();
      process_->waitForFinished(1000);
    }
  }
}

void PluginHost::onProcessFinished(int code, QProcess::ExitStatus status) {
  const bool was_ready = ready_;
  ready_ = false;
  if (socket_ != nullptr) socket_->deleteLater();
  socket_ = nullptr;
  buffer_.clear();
  authenticated_ = false;
  if (server_ != nullptr) server_->deleteLater();
  server_ = nullptr;
  process_->deleteLater();
  process_ = nullptr;
  if (was_ready) emit readyChanged(false);
  if (active_job_) {
    endJob(*active_job_, false, false,
           tr("the plugin process ended (exit code %1)").arg(code));
  }
  if (status == QProcess::CrashExit || (code != 0 && !was_ready)) {
    emit failed(tr("plugin '%1' stopped (exit code %2)%3")
                    .arg(manifest_.name)
                    .arg(code)
                    .arg(stderr_.isEmpty() ? QString() : QStringLiteral(":\n") + stderr_.right(800)));
  }
}

}  // namespace nfcad::plugins
