// Routing of the node workflow's block requests between plugins (ADR 0010, D2/D3): catalogues,
// evaluation by another plugin (started if needed), a provider that dies while evaluating.
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include "document/document.hpp"
#include "document/selection.hpp"
#include "plugins/block_router.hpp"
#include "plugins/plugin_host.hpp"
#include "plugins/plugin_manager.hpp"
#include "results/result_store.hpp"

using namespace nfcad;

class TestBlockRouter : public QObject {
  Q_OBJECT
  document::Document doc_;
  document::Selection selection_;
  results::ResultStore results_;
  std::unique_ptr<plugins::PluginHost> engine_, provider_;
  std::unique_ptr<plugins::BlockRouter> router_;
  QString python_;
  int starts_ = 0;

  std::unique_ptr<plugins::PluginHost> host(const char* dir) {
    QString error;
    const auto m = plugins::read_manifest(QStringLiteral(NFCAD_SOURCE_DIR "/tests/plugins/") + QString::fromLatin1(dir), &error);
    if (!m) qFatal("%s", qPrintable(error));
    return std::make_unique<plugins::PluginHost>(*m, &doc_, &selection_, &results_);
  }

  QJsonObject ask(int x, bool crash = false) {
    QSignalSpy finished(engine_.get(), &plugins::PluginHost::finished);
    engine_->run(QStringLiteral("ask"), QJsonObject{{"x", x}, {"crash", crash}});
    if (!finished.wait(30000)) return {};
    return QJsonDocument::fromJson(finished.first().at(3).toString().toUtf8()).object();
  }

 private slots:
  void initTestCase() {
    python_ = qEnvironmentVariable("NFCAD_TEST_PYTHON");
    if (python_.isEmpty()) python_ = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python_.isEmpty()) QSKIP("no Python interpreter found");
    selection_.attach(&doc_);
    engine_ = host("block_engine");
    provider_ = host("block_provider");
    router_ = std::make_unique<plugins::BlockRouter>(
        [this] { return std::vector<plugins::PluginHost*>{engine_.get(), provider_.get()}; },
        [this](plugins::PluginHost* h) {
          ++starts_;
          h->start(python_, plugins::PluginManager::nfcadPythonPath());
        });
    router_->attach(engine_.get());
    router_->attach(provider_.get());
    QSignalSpy ready(engine_.get(), &plugins::PluginHost::readyChanged);
    engine_->start(python_, plugins::PluginManager::nfcadPythonPath());
    QVERIFY(ready.wait(30000));
  }

  void providerStartedOnDemand() {
    QVERIFY(!provider_->running());
    const QJsonObject r = ask(21);
    QCOMPARE(starts_, 1);  // asking for the catalogues started the provider (its hello has them)
    QCOMPARE(r["answer"].toObject()["value"].toInt(), 42);
    const QJsonArray catalogues = r["catalogues"].toArray();
    QCOMPARE(catalogues.size(), 1);  // only the other plugins' blocks
    QCOMPARE(catalogues[0].toObject()["provider"].toString(), QStringLiteral("provider"));
    QCOMPARE(catalogues[0].toObject()["version"].toString(), QStringLiteral("2"));
    QCOMPARE(catalogues[0].toObject()["type"].toObject()["type"].toString(), QStringLiteral("provider.double"));
    QCOMPARE(router_->catalogues(provider_.get()).size(), 0);  // the engine has none
    QCOMPARE(router_->pending(), std::size_t{0});
  }

  void runningProviderAnswers() {
    QVERIFY(provider_->ready());
    QCOMPARE(ask(5)["answer"].toObject()["value"].toInt(), 10);
    QCOMPARE(starts_, 1);
  }

  void providerDyingFailsTheRequest() {
    const QJsonObject r = ask(1, true);
    QVERIFY2(r["answer"].toObject()["failed"].toString().contains(QStringLiteral("stopped")), qPrintable(QJsonDocument(r).toJson()));
    QCOMPARE(router_->pending(), std::size_t{0});
    QCOMPARE(ask(4)["answer"].toObject()["value"].toInt(), 8);  // started again on the next request
    QCOMPARE(starts_, 2);
  }

  void cleanupTestCase() {
    if (engine_) engine_->stop();
    if (provider_) provider_->stop();
  }
};

QTEST_MAIN(TestBlockRouter)
#include "test_block_router.moc"
