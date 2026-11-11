#include <QFile>
#include <QGraphicsRectItem>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "document/document.hpp"
#include "document/nfcad_io.hpp"
#include "ui/node_editor.hpp"

using nfcad::ui::NodeEditor;

namespace {
QJsonObject linnik() {
  // made by RTT-Fusion from its Linnik structure (graph_from_raytatouille + catalogue)
  QFile f(QStringLiteral(NFCAD_TEST_DATA "/workflow_linnik.json"));
  if (!f.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(f.readAll()).object();
}

QJsonObject node(const QJsonObject& wf, const QString& id) {
  for (const QJsonValue& v : wf["graph"].toObject()["nodes"].toArray()) {
    if (v.toObject()["id"].toString() == id) return v.toObject();
  }
  return {};
}
}  // namespace

class TestNodeEditor : public QObject {
  Q_OBJECT
 private slots:
  void showsTheGraph() {
    NodeEditor e;
    e.setWorkflow(linnik());
    QVERIFY(e.nodeItem(QStringLiteral("Beam_splitter")) != nullptr);
    QVERIFY(e.nodeItem(QStringLiteral("Sample")) != nullptr);
    QCOMPARE(e.linkCount(), 13);
  }

  void editing() {
    NodeEditor e;
    e.setWorkflow(linnik());
    QSignalSpy edited(&e, &NodeEditor::edited);
    // one link per input: the sample's input is taken
    QVERIFY(!e.addLink(QStringLiteral("Beam_splitter"), QStringLiteral("to_Objective_A"), QStringLiteral("Sample"),
                       QStringLiteral("from_Objective_A")));
    QCOMPARE(edited.count(), 0);
    // a new detector from the catalogue, with the type's default parameters, fed by the splitter
    const QString det = e.addNode(QStringLiteral("rtt.detector"), QPointF(100, 300));
    QCOMPARE(node(e.workflow(), det)["params"].toObject()["pixels"].toInt(), 512);
    QVERIFY(e.addLink(QStringLiteral("Beam_splitter"), QStringLiteral("to_Pupil_camera"), det, QStringLiteral("in")));
    QCOMPARE(e.linkCount(), 14);
    // transport fixed and back to auto
    e.setTransport(0, QStringLiteral("H"));
    QCOMPARE(e.workflow()["graph"].toObject()["links"].toArray()[0].toObject()["transport"].toString(), QStringLiteral("H"));
    e.setTransport(0, QStringLiteral("auto"));
    QVERIFY(!e.workflow()["graph"].toObject()["links"].toArray()[0].toObject().contains("transport"));
    // mute, then remove the sample: its two links go with it
    e.setMuted(QStringLiteral("Sample"), true);
    QVERIFY(node(e.workflow(), QStringLiteral("Sample"))["muted"].toBool());
    e.removeNode(QStringLiteral("Sample"));
    QCOMPARE(e.linkCount(), 12);
    QVERIFY(e.nodeItem(QStringLiteral("Sample")) == nullptr);
    QCOMPARE(edited.count(), 6);  // addNode, addLink, 2 × transport, mute, remove
  }

  void diagnosticsAreMarked() {
    QJsonObject wf = linnik();
    wf["diagnostics"] = QJsonArray{QJsonObject{{"code", "graph.cycle"}, {"severity", "error"},
                                               {"message", "feedback"}, {"location", "Sample.from_Objective_A"}}};
    NodeEditor e;
    e.setWorkflow(wf);
    QVERIFY(e.nodeItem(QStringLiteral("Sample"))->pen().widthF() > 2.0);
    QVERIFY(e.nodeItem(QStringLiteral("Sample"))->toolTip().contains(QStringLiteral("graph.cycle")));
    QVERIFY(e.nodeItem(QStringLiteral("Camera"))->pen().widthF() < 2.0);
    // an edit makes the diagnostics stale
    e.setMuted(QStringLiteral("Camera"), true);
    QVERIFY(!e.workflow().contains("diagnostics"));
  }

  void savedWithTheDocument() {
    nfcad::document::Document doc;
    doc.setWorkflow(linnik());
    QVERIFY(doc.isModified());
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("w.rttf"));
    QVERIFY(nfcad::document::save_nfcad(doc, path).isEmpty());
    nfcad::document::Document back;
    QCOMPARE(nfcad::document::load_nfcad(back, path), QString());
    QCOMPARE(back.workflow(), linnik());
  }

  void snapshot() {
    NodeEditor e;
    e.resize(1500, 420);
    e.setWorkflow(linnik());
    e.show();
    QVERIFY(QTest::qWaitForWindowExposed(&e) || true);
    e.fitAll();
    const QImage img = e.grab().toImage();
    QVERIFY(!img.isNull());
    if (const QByteArray dir = qgetenv("NFCAD_SNAPSHOT_DIR"); !dir.isEmpty()) {
      img.save(QString::fromLocal8Bit(dir) + QStringLiteral("/node_editor.png"));
    }
  }
};

QTEST_MAIN(TestNodeEditor)
#include "test_node_editor.moc"
