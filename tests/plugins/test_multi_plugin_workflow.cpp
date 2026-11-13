// ADR 0010, stage W2: one node workflow with blocks from two plugins – the engine (Workflow plugin)
// and a provider (a test plugin with RTT-Fusion's thin lens as "lensprov.thin_lens"). The
// workflow gives the same image as with the built-in lens in the engine's process; a provider that
// dies fails its block, the engine keeps running.
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>

#include "document/document.hpp"
#include "document/selection.hpp"
#include "plugins/block_router.hpp"
#include "plugins/plugin_host.hpp"
#include "plugins/plugin_manager.hpp"
#include "results/result_store.hpp"

using namespace nfcad;

namespace {
QJsonObject workflow(const QString& lens_type, bool crash = false) {
  QJsonObject lens_params{{"point", QJsonArray{0, 0, 0}}, {"focal_length", 40.0}};
  if (crash) lens_params["crash"] = true;
  const QJsonArray nodes{
      QJsonObject{{"id", "src"}, {"type", "optics.collimated_source"},
                  {"params", QJsonObject{{"radius", 1.0}, {"rings", 8}, {"center", QJsonArray{0, 0, -1}}}}},
      QJsonObject{{"id", "lens"}, {"type", lens_type}, {"params", lens_params}},
      QJsonObject{{"id", "cam"}, {"type", "optics.detector"},
                  {"params", QJsonObject{{"point", QJsonArray{0, 0, 20.0}}, {"normal", QJsonArray{0, 0, -1}},
                                         {"half_size", QJsonArray{1.5, 1.5}}, {"pixels", QJsonArray{32, 32}}}}},
      QJsonObject{{"id", "show"}, {"type", "display.image"}}};
  const QJsonArray links{
      QJsonObject{{"from", QJsonArray{"src", "out"}}, {"to", QJsonArray{"lens", "in"}}},
      QJsonObject{{"from", QJsonArray{"lens", "out"}}, {"to", QJsonArray{"cam", "in"}}},
      QJsonObject{{"from", QJsonArray{"cam", "image"}}, {"to", QJsonArray{"show", "in"}}}};
  return QJsonObject{{"graph", QJsonObject{{"format", "rtt-fusion-graph"}, {"version", 1}, {"nodes", nodes}, {"links", links}}}};
}
}  // namespace

class TestMultiPluginWorkflow : public QObject {
  Q_OBJECT
  document::Document doc_;
  document::Selection selection_;
  results::ResultStore results_;
  std::unique_ptr<plugins::PluginHost> engine_, provider_;
  std::unique_ptr<plugins::BlockRouter> router_;
  QString python_;
  QStringList log_;

  std::unique_ptr<plugins::PluginHost> host(const QString& dir) {
    QString error;
    const auto m = plugins::read_manifest(dir, &error);
    if (!m) qFatal("%s", qPrintable(error));
    return std::make_unique<plugins::PluginHost>(*m, &doc_, &selection_, &results_);
  }

  /// Runs the engine's Run workflow; ok and message.
  std::pair<bool, QString> run() {
    QSignalSpy finished(engine_.get(), &plugins::PluginHost::finished);
    engine_->run(QStringLiteral("run_workflow"), QJsonObject{{"rays", 200}, {"pixels", 32}, {"window", 0.0}, {"gaussian_waist", 0.0}});
    if (!finished.wait(120000)) return {false, QStringLiteral("timeout")};
    return {finished.first().at(1).toBool(), finished.first().at(3).toString()};
  }

  std::vector<float> lastImage() const {
    const auto& items = results_.sets().back().items;
    for (const auto& it : items) {
      if (it.kind == results::ResultItem::Kind::Image && it.image) return it.image->values;
    }
    return {};
  }

 private slots:
  void initTestCase() {
    python_ = qEnvironmentVariable("NFCAD_TEST_PYTHON");
    if (python_.isEmpty()) python_ = QStringLiteral(NFCAD_SOURCE_DIR "/.venv/bin/python");
    QProcess probe;
    probe.start(python_, {QStringLiteral("-c"), QStringLiteral("import rtt_fusion.graph.remote, rtt_fusion.graph.workflow")});
    if (!probe.waitForFinished(30000) || probe.exitCode() != 0) QSKIP("no Python with rtt_fusion and graph.remote");
    selection_.attach(&doc_);
    engine_ = host(QStringLiteral(NFCAD_SOURCE_DIR "/python/plugins/workflow"));
    provider_ = host(QStringLiteral(NFCAD_SOURCE_DIR "/tests/plugins/lens_provider"));
    connect(engine_.get(), &plugins::PluginHost::logLine, this, [this](const QString& l) { log_ << l; });
    router_ = std::make_unique<plugins::BlockRouter>(
        [this] { return std::vector<plugins::PluginHost*>{engine_.get(), provider_.get()}; },
        [this](plugins::PluginHost* h) { h->start(python_, plugins::PluginManager::nfcadPythonPath()); });
    router_->attach(engine_.get());
    router_->attach(provider_.get());
    QSignalSpy ready(engine_.get(), &plugins::PluginHost::readyChanged);
    engine_->start(python_, plugins::PluginManager::nfcadPythonPath());
    QVERIFY(ready.wait(30000));
  }

  void blocksOfTwoPluginsEqualOneProcess() {
    doc_.setWorkflow(workflow(QStringLiteral("optics.thin_lens")));  // the engine's own lens
    auto [ok_local, msg_local] = run();
    QVERIFY2(ok_local, qPrintable(msg_local));
    const std::vector<float> local = lastImage();
    QVERIFY(!local.empty());
    doc_.setWorkflow(workflow(QStringLiteral("lensprov.thin_lens")));  // the provider's lens
    auto [ok_remote, msg_remote] = run();
    QVERIFY2(ok_remote, qPrintable(msg_remote));
    QVERIFY(provider_->ready());  // started for its blocks
    const std::vector<float> remote = lastImage();
    QCOMPARE(remote.size(), local.size());
    QVERIFY(remote == local);  // bit for bit
    float sum = 0;
    for (float v : remote) sum += v;
    QVERIFY(sum > 0);  // content guard: light arrives
  }

  void aDyingProviderFailsItsBlock() {
    log_.clear();
    doc_.setWorkflow(workflow(QStringLiteral("lensprov.thin_lens"), true));
    const auto [ok, msg] = run();
    QVERIFY2(msg.contains(QStringLiteral("nothing computed")), qPrintable(msg));
    QVERIFY2(log_.join(QLatin1Char('\n')).contains(QStringLiteral("stopped")), qPrintable(log_.join(QLatin1Char('\n'))));
    QVERIFY(engine_->ready());  // the engine keeps running
    doc_.setWorkflow(workflow(QStringLiteral("lensprov.thin_lens")));
    QVERIFY(run().first);  // the provider is started again
  }

  void cleanupTestCase() {
    if (engine_) engine_->stop();
    if (provider_) provider_->stop();
  }
};

QTEST_MAIN(TestMultiPluginWorkflow)
#include "test_multi_plugin_workflow.moc"
