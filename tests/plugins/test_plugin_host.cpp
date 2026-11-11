#include <QJsonArray>
#include <QJsonObject>
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
    QCOMPARE(host_->actions().size(), std::size_t{7});
    QCOMPARE(host_->paramsSchema().size(), 5);
    QCOMPARE(host_->paramsSchema()[0].toObject()["name"].toString(), QStringLiteral("count"));  // order kept
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
    QCOMPARE(items.size(), std::size_t{4});
    QCOMPARE(items[3].kind, results::ResultItem::Kind::Image);
    QCOMPARE(items[3].image->width, 3);
    QCOMPARE(items[3].image->x0, -1.5);
    QCOMPARE(items[3].image->value(2, 1), 5.0F);
    QCOMPARE(items[3].image->description, QStringLiteral("6 px"));
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

  void groupsAndRemovalOneUndoStep() {
    // after modelAndResultsOneUndoStep: "target" and "from plugin"
    QCOMPARE(doc_.entities().size(), std::size_t{2});
    const document::Entity* from_plugin = nullptr;
    for (const auto& [id, e] : doc_.entities()) from_plugin = e.name == QStringLiteral("from plugin") ? &e : from_plugin;
    QVERIFY(from_plugin != nullptr);
    const document::EntityId removed_id = from_plugin->id;
    const document::GeometryPtr removed_geometry = from_plugin->geometry;
    QSignalSpy finished(host_.get(), &plugins::PluginHost::finished);
    QVERIFY(host_->run(QStringLiteral("tree")) > 0);
    QVERIFY(waitFinished(finished));
    QVERIFY2(finished.first().at(1).toBool(), qPrintable(finished.first().at(3).toString()));
    QCOMPARE(doc_.groups().size(), std::size_t{2});
    QCOMPARE(doc_.entity(removed_id), nullptr);
    QCOMPARE(doc_.entities().size(), std::size_t{2});  // target + "in lens"
    const document::Entity* in_lens = nullptr;
    for (const auto& [id, e] : doc_.entities()) in_lens = e.name == QStringLiteral("in lens") ? &e : in_lens;
    QVERIFY(in_lens != nullptr);
    const document::GroupId lens = in_lens->group;
    QCOMPARE(doc_.group(lens)->name, QStringLiteral("lens"));
    QCOMPARE(doc_.group(doc_.group(lens)->parent)->name, QStringLiteral("system"));
    // one undo step: groups gone, the removed entity back with its geometry from before the run
    doc_.undoStack()->undo();
    QCOMPARE(doc_.groups().size(), std::size_t{0});
    QCOMPARE(doc_.entities().size(), std::size_t{2});
    QVERIFY(doc_.entity(removed_id) != nullptr);
    QVERIFY(doc_.entity(removed_id)->geometry == removed_geometry);
    doc_.undoStack()->redo();
    QCOMPARE(doc_.groups().size(), std::size_t{2});
    QCOMPARE(doc_.entity(removed_id), nullptr);
    QVERIFY(doc_.group(lens) != nullptr);  // same ids again
  }

  void parametersReachThePlugin() {
    QSignalSpy finished(host_.get(), &plugins::PluginHost::finished);
    // count from CAD as a JSON number, the rest defaults; types converted by nfcad.Param
    host_->run(QStringLiteral("echo"), QJsonObject{{"count", 7}, {"target", QJsonArray{42}}});
    QVERIFY(waitFinished(finished));
    QCOMPARE(finished.first().at(3).toString(), QStringLiteral("7|1.5|(1.0, 2.0, 3.0)|42|[1.0, 2.0]"));
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

  void userErrorIsAMessage() {
    QSignalSpy finished(host_.get(), &plugins::PluginHost::finished);
    QSignalSpy log(host_.get(), &plugins::PluginHost::logLine);
    QVERIFY(host_->run(QStringLiteral("needs_input")) > 0);
    QVERIFY(waitFinished(finished));
    QCOMPARE(finished.first().at(1).toBool(), false);
    QCOMPARE(finished.first().at(3).toString(), QStringLiteral("Choose a file first."));  // no type name
    QCOMPARE(finished.first().at(4).toBool(), true);
    for (const auto& l : log) QVERIFY(!l.at(0).toString().contains(QStringLiteral("Traceback")));
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
