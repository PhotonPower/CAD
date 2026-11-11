#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTest>
#include <nurbforge/nurbforge.hpp>

#include "document/document.hpp"
#include "document/selection.hpp"
#include "plugins/plugin_manager.hpp"
#include "results/result_store.hpp"

using namespace nfcad;

namespace {
QString find_python() {
  const QString env = qEnvironmentVariable("NFCAD_TEST_PYTHON");
  if (!env.isEmpty()) return env;
  for (const char* n : {"python3", "python"}) {
    const QString p = QStandardPaths::findExecutable(QString::fromLatin1(n));
    if (!p.isEmpty()) return p;
  }
  return {};
}
}  // namespace

class TestPluginHost : public QObject {
  Q_OBJECT
  document::Document doc_;
  document::Selection selection_;
  results::ResultStore results_;
  std::unique_ptr<plugins::PluginHost> host_;
  QString python_;

  bool waitFinished(QSignalSpy& spy, int ms = 20000) { return !spy.isEmpty() || spy.wait(ms); }

 private slots:
  void initTestCase() {
    python_ = find_python();
    if (python_.isEmpty()) QSKIP("no Python interpreter found");
    selection_.attach(&doc_);
    QString error;
    const auto m = plugins::read_manifest(QStringLiteral(NFCAD_TEST_PLUGIN_DIR), &error);
    QVERIFY2(m.has_value(), qPrintable(error));
    QCOMPARE(m->id, QStringLiteral("test"));
    host_ = std::make_unique<plugins::PluginHost>(*m, &doc_, &selection_, &results_);
    QSignalSpy ready(host_.get(), &plugins::PluginHost::readyChanged);
    QSignalSpy failed(host_.get(), &plugins::PluginHost::failed);
    host_->start(python_, plugins::PluginManager::nfcadPythonPath());
    QVERIFY2(ready.wait(20000), qPrintable(failed.isEmpty() ? QStringLiteral("timeout")
                                                            : failed.first().at(0).toString()));
    QCOMPARE(host_->actions().size(), std::size_t{4});
  }

  void foreignConnectionsAreRejected() {
    QTcpSocket intruder;
    intruder.connectToHost(QHostAddress::LocalHost, host_->channelPort());
    QVERIFY(intruder.waitForConnected(5000));
    intruder.write("{\"type\":\"auth\",\"token\":\"wrong\"}\n{\"type\":\"request\",\"rid\":1,\"method\":\"model.entities\",\"args\":{}}\n");
    intruder.flush();
    QTRY_COMPARE_WITH_TIMEOUT(intruder.state(), QAbstractSocket::UnconnectedState, 5000);
    QVERIFY(intruder.readAll().isEmpty());  // no reply with model data
    QVERIFY(host_->ready());                // the real plugin keeps working
  }

  void manifestErrors() {
    QString error;
    QVERIFY(!plugins::read_manifest(QStringLiteral("/nonexistent"), &error));
    QVERIFY(!error.isEmpty());
  }

  void modelAndResultsOneUndoStep() {
    const auto ids = doc_.addEntities({document::make_entity(nf::make_line({0, 0, 0}, {1, 0, 0}), QStringLiteral("target"))},
                                      QStringLiteral("add"));
    selection_.apply({{ids[0], {}}}, document::Selection::Mode::Replace);
    QSignalSpy finished(host_.get(), &plugins::PluginHost::finished);
    QSignalSpy log(host_.get(), &plugins::PluginHost::logLine);
    QVERIFY(host_->run(QStringLiteral("model")) > 0);
    QVERIFY(waitFinished(finished));
    QVERIFY2(finished.first().at(1).toBool(), qPrintable(finished.first().at(3).toString()));
    QVERIFY(finished.first().at(3).toString().startsWith(QStringLiteral("new ")));
    // model changes
    QCOMPARE(doc_.entities().size(), std::size_t{2});
    const auto& line = std::get<nf::NurbsCurve>(*doc_.entity(ids[0])->geometry);
    QVERIFY(nf::distance(line.evaluate(line.domain_end()), {0, 0, 7}) < 1e-12);
    QCOMPARE(doc_.entity(ids[0])->attributes.value(QStringLiteral("rtt.role")).toString(), QStringLiteral("mirror"));
    // results (arrays through files)
    QCOMPARE(results_.sets().size(), std::size_t{1});
    const auto& items = results_.sets().front().items;
    QCOMPARE(items.size(), std::size_t{3});
    QCOMPARE(items[0].lines->polylineCount(), std::size_t{2});
    QCOMPARE(items[0].lines->values.size(), std::size_t{4});
    QCOMPARE(items[1].field->values.size(), std::size_t{6});
    QCOMPARE(items[2].curve->y.size(), std::size_t{2});  // incl. the appended point
    bool logged = false;
    for (const auto& l : log) logged = logged || l.at(0).toString().contains(QStringLiteral("hello from the plugin"));
    QVERIFY(logged);
    // one undo step reverts the whole run
    doc_.undoStack()->undo();
    QCOMPARE(doc_.entities().size(), std::size_t{1});
    const auto& orig = std::get<nf::NurbsCurve>(*doc_.entity(ids[0])->geometry);
    QVERIFY(nf::distance(orig.evaluate(orig.domain_end()), {1, 0, 0}) < 1e-12);
    QVERIFY(doc_.entity(ids[0])->attributes.isEmpty());
    doc_.undoStack()->redo();
    QCOMPARE(doc_.entities().size(), std::size_t{2});
  }

  void cancel() {
    QSignalSpy finished(host_.get(), &plugins::PluginHost::finished);
    QVERIFY(host_->run(QStringLiteral("loop")) > 0);
    QTest::qWait(200);
    host_->cancel();
    QVERIFY(waitFinished(finished));
    QVERIFY(!finished.first().at(1).toBool());
    QVERIFY(finished.first().at(2).toBool());  // cancelled
    QVERIFY(host_->running());                 // the process survives a cancelled job
  }

  void pythonError() {
    QSignalSpy finished(host_.get(), &plugins::PluginHost::finished);
    host_->run(QStringLiteral("fail"));
    QVERIFY(waitFinished(finished));
    QVERIFY(!finished.first().at(1).toBool());
    QVERIFY(finished.first().at(3).toString().contains(QStringLiteral("deliberate")));
  }

  void crashIsContained() {
    QSignalSpy finished(host_.get(), &plugins::PluginHost::finished);
    QSignalSpy failed(host_.get(), &plugins::PluginHost::failed);
    host_->run(QStringLiteral("crash"));
    QVERIFY(waitFinished(finished));
    QVERIFY(!finished.first().at(1).toBool());
    QVERIFY(finished.first().at(3).toString().contains(QStringLiteral("exit code 3")));
    QVERIFY(!host_->running());
    // the next run starts the process again
    QSignalSpy ready(host_.get(), &plugins::PluginHost::readyChanged);
    host_->start(python_, plugins::PluginManager::nfcadPythonPath());
    QVERIFY(ready.wait(20000));
  }

  void cleanupTestCase() {
    if (host_) host_->stop();
  }
};

QTEST_GUILESS_MAIN(TestPluginHost)
#include "test_plugin_host.moc"
