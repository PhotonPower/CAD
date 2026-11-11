#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>
#include <map>
#include <optional>

#include "document/document.hpp"

class QTcpServer;
class QTcpSocket;

namespace nfcad::document {
class Selection;
}
namespace nfcad::results {
class ResultStore;
}

namespace nfcad::plugins {

/// Contents of a plugin's plugin.json (docs/plugin-api.md).
struct Manifest {
  QString id;
  QString name;
  QString version;
  int api = 0;
  QString entry;        ///< "module:Class"
  QString description;
  QString directory;    ///< directory the plugin runs from (for signed plugins a private, checked copy)
  QString source_directory;  ///< the plugin's folder as installed
  bool trusted = false;      ///< signature valid with a built-in publisher key (P5)
  QString trust_note;        ///< why it is not trusted ("not signed", …); developer mode only

  /// Name for menus and panels; untrusted plugins (developer mode) are marked.
  [[nodiscard]] QString displayName() const {
    return trusted ? name : QStringLiteral("%1 [%2]").arg(name, trust_note);
  }
};

/// Reads <dir>/plugin.json; empty on errors (message in `error`).
[[nodiscard]] std::optional<Manifest> read_manifest(const QString& dir, QString* error = nullptr);

/// Plugin API version CAD implements (python/nfcad/plugin.py, API_VERSION).
inline constexpr int kPluginApiVersion = 1;

/// One plugin in its own Python process (docs/plugins.md, D2): starts
/// `python -m nfcad.host --server <local server> --plugin <dir>`, speaks JSON lines with it,
/// runs actions as jobs (progress, log, cancel), and turns its requests into document and
/// result changes. All model changes of one job form one undo step.
class PluginHost : public QObject {
  Q_OBJECT
 public:
  struct Action {
    QString id;
    QString label;
    QString description;
  };

  PluginHost(Manifest manifest, document::Document* doc, document::Selection* selection,
             results::ResultStore* results, QObject* parent = nullptr);
  ~PluginHost() override;

  [[nodiscard]] const Manifest& manifest() const { return manifest_; }
  [[nodiscard]] bool running() const;
  [[nodiscard]] bool ready() const { return ready_; }
  [[nodiscard]] const std::vector<Action>& actions() const { return actions_; }
  /// Parameter descriptions from the plugin (nfcad.Param, in display order).
  [[nodiscard]] QJsonArray paramsSchema() const { return params_; }
  [[nodiscard]] std::optional<int> activeJob() const { return active_job_; }
  /// TCP port of the plugin channel on 127.0.0.1 (0 if not listening; for tests).
  [[nodiscard]] quint16 channelPort() const;

  /// Starts the process (no-op if running). `python` is the interpreter, `pythonpath` the
  /// directory that contains the nfcad package.
  void start(const QString& python, const QString& pythonpath);
  /// Runs an action (starts the process first if needed); returns the job id, 0 if busy.
  int run(const QString& action, const QJsonObject& params = {});
  /// Asks the action to stop; kills the process if it does not end within `grace_ms`.
  void cancel(int grace_ms = 5000);
  void stop();

 signals:
  void readyChanged(bool ready);
  void progress(int job, double value, const QString& text);
  void logLine(const QString& text);
  /// A job ended: ok, cancelled, message (result text or error).
  void finished(int job, bool ok, bool cancelled, const QString& message);
  /// The process ended unexpectedly or could not start.
  void failed(const QString& message);

 private:
  void onConnection();
  void onReadyRead();
  void handle(const QJsonObject& msg);
  void reply(int rid, bool ok, const QJsonValue& result, const QString& error = {});
  QJsonValue request(const QString& method, const QJsonObject& args);  // throws std::runtime_error
  void send(const QJsonObject& msg);
  void beginJob(int job);
  void endJob(int job, bool ok, bool cancelled, const QString& message);
  void onProcessFinished(int code, QProcess::ExitStatus status);
  [[nodiscard]] QString arrayDir() const;

  Manifest manifest_;
  document::Document* doc_;
  document::Selection* selection_;
  results::ResultStore* results_;
  QProcess* process_ = nullptr;
  QTcpServer* server_ = nullptr;
  QPointer<QTcpSocket> socket_;
  QByteArray buffer_;
  QByteArray token_;           // shared secret of this process (NFCAD_TOKEN)
  std::unique_ptr<QTemporaryDir> array_dir_;  // the only place array files are accepted from
  bool authenticated_ = false;
  bool ready_ = false;
  std::vector<Action> actions_;
  QJsonArray params_;
  std::optional<std::pair<QString, QJsonObject>> queued_;  // run requested before ready
  std::optional<int> active_job_;
  int next_job_ = 1;
  QString stderr_;
  // model changes of the active job (one undo step at the end)
  std::map<document::EntityId, document::GeometryPtr> original_geometry_;
  std::map<document::EntityId, QVariantMap> original_attributes_;
  std::vector<document::EntityId> added_;
};

}  // namespace nfcad::plugins
