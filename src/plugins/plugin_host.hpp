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
#include <tuple>

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
  QStringList requirements;  ///< Python packages, "name" or "name>=version" (import name = distribution name)
  QString update_url;    ///< optional: JSON {"version": …, "url": …} with the newest release (P7)
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
    bool on_workflow_edit = false;  ///< also run quietly after an edit of the node workflow
    QStringList block_types;        ///< an action on one block of these types (its context menu)
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
  /// `target`: the block an action with block types was chosen on ({"block", "scope"}).
  int run(const QString& action, const QJsonObject& params = {}, const QJsonObject& target = {});
  /// Asks the action to stop; kills the process if it does not end within `grace_ms`.
  void cancel(int grace_ms = 5000);
  void stop();

 signals:
  void readyChanged(bool ready);
  void progress(int job, double value, const QString& text);
  void logLine(const QString& text);
  /// A job ended: ok, cancelled, message (result text or error).
  /// `user_error`: the plugin raised nfcad.UserError – a message for the user, not a bug.
  void finished(int job, bool ok, bool cancelled, const QString& message, bool user_error = false);
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
  void endJob(int job, bool ok, bool cancelled, const QString& message, bool user_error = false);
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
  std::optional<std::tuple<QString, QJsonObject, QJsonObject>> queued_;  // run requested before ready
  std::optional<int> active_job_;
  int next_job_ = 1;
  QString stderr_;
  // model changes of the active job (one undo step at the end)
  std::map<document::EntityId, document::GeometryPtr> original_geometry_;
  std::map<document::EntityId, QVariantMap> original_attributes_;
  std::optional<QJsonObject> original_workflow_;  ///< before the run's first workflow.set
  std::vector<document::EntityId> added_;
  std::vector<document::Entity> removed_;          ///< state before the job
  std::vector<document::GroupId> added_groups_;
  std::vector<document::Group> removed_groups_;
};

}  // namespace nfcad::plugins
