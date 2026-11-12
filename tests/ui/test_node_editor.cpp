#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QTimer>
#include <QFile>
#include <QGraphicsRectItem>
#include <QImage>
#include <QJsonArray>
#include <QListWidget>
#include <QTabWidget>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QLineF>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>

#include <cmath>

#include "document/document.hpp"
#include "document/nfcad_io.hpp"
#include "ui/branch_panel.hpp"
#include "ui/node_editor.hpp"

using nfcad::ui::NodeEditor;

namespace {
QJsonObject linnik() {
  // made by RTT-Fusion from its Linnik structure (graph_from_raytatouille + catalogue)
  QFile f(QStringLiteral(NFCAD_TEST_DATA "/workflow_linnik.json"));
  if (!f.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(f.readAll()).object();
}

QJsonObject model_linnik() {
  // made by the Raytracer plugin from RTT-Fusion's Linnik structure (R24: model, branches)
  QFile f(QStringLiteral(NFCAD_TEST_DATA "/workflow_model_linnik.json"));
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

  void planIsShown() {
    // the planner's result (RTT-Fusion R17): representation per link, conversions per block
    QJsonObject wf = linnik();
    const QJsonObject l0 = wf["graph"].toObject()["links"].toArray()[0].toObject();
    const QString key = QStringLiteral("%1.%2 -> %3.%4")
                            .arg(l0["from"].toArray()[0].toString(), l0["from"].toArray()[1].toString(),
                                 l0["to"].toArray()[0].toString(), l0["to"].toArray()[1].toString());
    const QString node = l0["to"].toArray()[0].toString();
    wf["plan"] = QJsonObject{
        {"links", QJsonObject{{key, QJsonObject{{"representation", "H"}, {"reason", "only H admissible"}}}}},
        {"conversions", QJsonArray{QJsonObject{{"node", node}, {"from", "a"}, {"to", "b"}, {"chain", "HR"},
                                               {"operators", QJsonArray{"field_to_rays"}}}}}};
    wf["diagnostics"] = QJsonArray{QJsonObject{{"code", "plan.no_operator"}, {"severity", "error"},
                                               {"message", "no operator"}, {"location", key}}};
    NodeEditor e;
    e.setWorkflow(wf);
    QCOMPARE(e.plannedRepresentation(0), QStringLiteral("H"));
    QCOMPARE(e.plannedRepresentation(1), QString());
    QVERIFY(e.linkItem(0)->toolTip().contains(QStringLiteral("planned: H – only H admissible")));
    QVERIFY(e.linkItem(0)->toolTip().contains(QStringLiteral("plan.no_operator")));
    QCOMPARE(e.linkItem(0)->pen().style(), Qt::DashLine);
    QCOMPARE(e.linkItem(1)->pen().style(), Qt::SolidLine);
    QVERIFY(e.nodeItem(node)->toolTip().contains(QStringLiteral("a → b: HR (field_to_rays)")));
    // an edit makes the plan stale: it stays visible, grey, until the next check
    e.setMuted(node, true);
    QVERIFY(e.workflow()["plan"].toObject()["stale"].toBool());
    QCOMPARE(e.plannedRepresentation(0), QStringLiteral("H"));
    QVERIFY(e.linkItem(0)->toolTip().contains(QStringLiteral("before the last edit")));
  }

  void replaceAndParameters() {
    // concept 1: the sample mirror becomes a FourierGrating sample, the links stay
    NodeEditor e;
    e.setWorkflow(linnik());
    QVERIFY(e.replacementsFor(QStringLiteral("Sample")).contains(QStringLiteral("fusion.grating")));
    QVERIFY(!e.replacementsFor(QStringLiteral("Beam_splitter")).contains(QStringLiteral("fusion.grating")));
    QSignalSpy edited(&e, &NodeEditor::edited);
    QVERIFY(e.replaceNode(QStringLiteral("Sample"), QStringLiteral("fusion.grating")));
    QCOMPARE(edited.count(), 1);
    QCOMPARE(e.linkCount(), 13);
    const QJsonObject sample = node(e.workflow(), QStringLiteral("Sample"));
    QCOMPARE(sample["type"].toString(), QStringLiteral("fusion.grating"));
    QVERIFY(sample["params"].toObject().contains(QStringLiteral("table")));
    int into = 0, out_of = 0;
    for (const QJsonValue& v : e.workflow()["graph"].toObject()["links"].toArray()) {
      const QJsonObject l = v.toObject();
      if (l["to"].toArray() == QJsonArray{QStringLiteral("Sample"), QStringLiteral("in")}) ++into;
      if (l["from"].toArray() == QJsonArray{QStringLiteral("Sample"), QStringLiteral("out")}) ++out_of;
    }
    QCOMPARE(into, 1);
    QCOMPARE(out_of, 1);
    // parameters: set directly and through the dialog
    e.setParams(QStringLiteral("Sample"), QJsonObject{{"table", "/tmp/grating.json"}, {"orders", QJsonArray{-1, 0, 1}}});
    QCOMPARE(node(e.workflow(), QStringLiteral("Sample"))["params"].toObject()["table"].toString(), QStringLiteral("/tmp/grating.json"));
    QTimer::singleShot(0, &e, [] {
      for (QWidget* w : QApplication::topLevelWidgets()) {
        auto* d = qobject_cast<QDialog*>(w);
        if (d == nullptr || !d->isVisible()) continue;
        for (QLineEdit* line : d->findChildren<QLineEdit*>()) {
          if (line->text() == QStringLiteral("/tmp/grating.json")) line->setText(QStringLiteral("/tmp/other.json"));
        }
        d->accept();
      }
    });
    e.editParams(QStringLiteral("Sample"));
    const QJsonObject p = node(e.workflow(), QStringLiteral("Sample"))["params"].toObject();
    QCOMPARE(p["table"].toString(), QStringLiteral("/tmp/other.json"));
    QCOMPARE(p["orders"].toArray(), (QJsonArray{-1, 0, 1}));
  }

  void linksCanBeHit() {
    // a link is hit a few pixels beside the curve and on its label (right-click: transport)
    NodeEditor e;
    e.resize(1500, 420);
    e.setAttribute(Qt::WA_DontShowOnScreen);
    e.setWorkflow(linnik());
    e.show();
    e.fitAll();
    auto* link = e.linkItem(0);
    QVERIFY(link != nullptr);
    const QPointF mid = link->path().pointAtPercent(0.5);
    const QPoint at = e.mapFromScene(mid);
    QCOMPARE(e.linkAt(at), 0);
    QCOMPARE(e.linkAt(at + QPoint(0, 4)), 0);
    // the label of the planned representation sits in the middle of the link
    QVERIFY(!link->childItems().isEmpty());
    const QPoint on_label = e.mapFromScene(link->childItems().front()->sceneBoundingRect().center());
    QCOMPARE(e.linkAt(on_label), 0);
    QCOMPARE(e.linkAt(QPoint(5, 5)), -1);
  }

  void displayBlocksAndRunning() {
    QJsonObject wf = linnik();
    QJsonObject g = wf["graph"].toObject();
    QJsonArray nodes = g["nodes"].toArray();
    nodes.append(QJsonObject{{"id", "show"}, {"type", "display.image"}, {"name", "Camera window"}, {"position", QJsonArray{0, 600}}});
    g["nodes"] = nodes;
    wf["graph"] = g;
    QJsonArray types = wf["types"].toArray();
    types.append(QJsonObject{{"type", "display.image"}, {"category", "analysis"},
                             {"sockets", QJsonArray{QJsonObject{{"name", "in"}, {"direction", "in"}, {"kind", "data"}, {"accepts", QJsonArray{"result"}}},
                                                    QJsonObject{{"name", "view"}, {"direction", "out"}, {"kind", "data"}, {"accepts", QJsonArray{"result"}}}}}});
    wf["types"] = types;
    NodeEditor e;
    e.resize(1500, 600);
    e.setAttribute(Qt::WA_DontShowOnScreen);
    e.setWorkflow(wf);
    e.show();
    e.fitAll();
    QSignalSpy requested(&e, &NodeEditor::displayRequested);
    const QPoint at = e.mapFromScene(e.nodeItem(QStringLiteral("show"))->sceneBoundingRect().center());
    QTest::mouseDClick(e.viewport(), Qt::LeftButton, {}, at);
    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.at(0).at(0).toString(), QStringLiteral("show"));
    QCOMPARE(requested.at(0).at(1).toString(), QStringLiteral("display.image"));
    // the running block: a bright, wide frame
    e.setRunning(QStringLiteral("Sample"));
    QVERIFY(e.nodeItem(QStringLiteral("Sample"))->pen().widthF() >= 3.0);
    e.setRunning({});
    QVERIFY(e.nodeItem(QStringLiteral("Sample"))->pen().widthF() < 2.0);
  }

  void upToDateMarks() {
    // green tick / red cross: an edit marks the block and everything behind it out of date
    NodeEditor e;
    e.setWorkflow(linnik());
    QJsonObject all;
    for (const QJsonValue& v : e.workflow()["graph"].toObject()["nodes"].toArray()) all[v.toObject()["id"].toString()] = true;
    e.setStates(all);
    QCOMPARE(e.state(QStringLiteral("Sample")), 1);
    e.setParams(QStringLiteral("Sample"), QJsonObject{{"offset", QJsonArray{0, 0, 0.001}}});
    for (const char* stale : {"Sample", "Objective_A", "Beam_splitter", "Camera", "Pupil_camera"}) {
      QCOMPARE(e.state(QString::fromLatin1(stale)), 0);
    }
    QCOMPARE(e.state(QStringLiteral("source")), 1);  // in front of the change
    QCOMPARE(e.state(QStringLiteral("Illumination_stop")), 1);
    // moving a block changes nothing
    QJsonObject g = e.workflow()["graph"].toObject();
    e.setStates(all);
    QJsonArray nodes = g["nodes"].toArray();
    QJsonObject n0 = nodes[0].toObject();
    n0["position"] = QJsonArray{999, 999};
    nodes[0] = n0;
    g["nodes"] = nodes;
    QJsonObject wf = e.workflow();
    wf["graph"] = g;
    e.setWorkflow(wf);  // not an edit
    e.setStates(all);
    e.setMuted(QStringLiteral("Reference"), true);
    QCOMPARE(e.state(QStringLiteral("Reference")), 0);
    QCOMPARE(e.state(QStringLiteral("Sample")), 1);  // the other arm
    e.setStates({});
    QCOMPARE(e.state(QStringLiteral("Sample")), -1);
  }

  // ---------------------------------------------------------------- model (RTT-Fusion R24)

  void modelShowsPorts() {
    NodeEditor e;
    e.setWorkflow(model_linnik());
    QVERIFY(e.modelMode());
    QCOMPARE(e.connectionCount(), 9);  // one per pair of neighbours (13 directed links before)
    QCOMPARE(e.linkCount(), 0);        // no data links yet
    for (int k = 0; k < 9; ++k) QVERIFY(e.connectionItem(k) != nullptr);
    // the objective: one point per port, light comes in from the splitter (left), the sample right
    QVERIFY(e.portOnLeft(QStringLiteral("Objective_A"), QStringLiteral("Beam_splitter")));
    QVERIFY(!e.portOnLeft(QStringLiteral("Objective_A"), QStringLiteral("Sample")));
    QCOMPARE(e.branchNames(), (QStringList{"measurement", "reference", "measurement_pupil"}));
    if (const QByteArray dir = qgetenv("NFCAD_SNAPSHOT_DIR"); !dir.isEmpty()) {
      e.resize(1400, 700);
      e.highlightBranch(QStringLiteral("measurement"));
      e.fitAll();
      e.grab().toImage().save(QString::fromLocal8Bit(dir) + QStringLiteral("/node_editor_model.png"));
      e.scene()->clearSelection();
      for (const char* id : {"Beam_splitter", "Objective_A", "Sample", "Objective_B"}) e.nodeItem(QString::fromLatin1(id))->setSelected(true);
      e.zoomSelected();
      e.grab().toImage().save(QString::fromLocal8Bit(dir) + QStringLiteral("/node_editor_model_zoom.png"));
    }
  }

  void connectionsFollowMovedBlocks() {
    NodeEditor e;
    e.setWorkflow(model_linnik());
    int index = -1;  // the connection objective A – sample
    const QJsonArray connections = e.workflow()["model"].toObject()["connections"].toArray();
    for (int k = 0; k < connections.size(); ++k) {
      if (connections[k].toObject()["ports"].toArray().contains(QJsonArray{"Sample", "Objective_A"})) index = k;
    }
    QVERIFY(index >= 0);
    const QPointF end_before = e.connectionItem(index)->path().pointAtPercent(1.0);
    const QPointF start_before = e.connectionItem(index)->path().pointAtPercent(0.0);
    e.nodeItem(QStringLiteral("Sample"))->moveBy(80.0, 120.0);  // as a drag does
    const QPointF end = e.connectionItem(index)->path().pointAtPercent(1.0);
    const QPointF start = e.connectionItem(index)->path().pointAtPercent(0.0);
    // the end at the sample moved with it, the end at the objective stayed
    const bool sample_last = QLineF(end, end_before + QPointF(80.0, 120.0)).length() < 1e-6;
    const bool sample_first = QLineF(start, start_before + QPointF(80.0, 120.0)).length() < 1e-6;
    QVERIFY(sample_last || sample_first);
    QVERIFY(sample_last ? QLineF(start, start_before).length() < 1e-6 : QLineF(end, end_before).length() < 1e-6);
  }

  void connectionDirections() {
    // per direction a branch uses: an arrow and the planned representation; the tooltip names
    // the blocks; after an edit the plan stays, marked out of date
    NodeEditor e;
    e.setWorkflow(model_linnik());
    auto index_of = [&e](const QJsonArray& a, const QJsonArray& b) {
      const QJsonArray connections = e.workflow()["model"].toObject()["connections"].toArray();
      for (int k = 0; k < connections.size(); ++k) {
        const QJsonArray ends = connections[k].toObject()["ports"].toArray();
        if (ends.contains(a) && ends.contains(b)) return k;
      }
      return -1;
    };
    auto arrows = [&e](int k) {
      QStringList out;
      for (QGraphicsItem* child : e.connectionItem(k)->childItems()) {
        for (QGraphicsItem* t : child->childItems()) {
          if (auto* text = dynamic_cast<QGraphicsSimpleTextItem*>(t)) out << text->text();
        }
      }
      return out;
    };
    const int both = index_of(QJsonArray{"Objective_A", "Sample"}, QJsonArray{"Sample", "Objective_A"});
    const int one = index_of(QJsonArray{"source", "Illumination_stop"}, QJsonArray{"Illumination_stop", "source"});
    QVERIFY(both >= 0 && one >= 0);
    QStringList two = arrows(both);
    QCOMPARE(two.size(), 2);  // there and back to the sample
    QVERIFY(two.join(QLatin1Char(' ')).contains(QStringLiteral("▶")) && two.join(QLatin1Char(' ')).contains(QStringLiteral("◀")));
    QCOMPARE(arrows(one).size(), 1);  // the source only sends
    QVERIFY(e.connectionItem(both)->toolTip().contains(QStringLiteral("Objective A → Sample: R")));
    QVERIFY(e.connectionItem(both)->toolTip().contains(QStringLiteral("Sample → Objective A: R")));
    e.setParams(QStringLiteral("Sample"), QJsonObject{{"offset", QJsonArray{0, 0, 0.001}}});
    QVERIFY(e.connectionItem(both)->toolTip().contains(QStringLiteral("(out of date)")));
    QCOMPARE(arrows(both).size(), 2);  // still shown
    // a fixed transport shows with a star
    e.setConnectionTransport(both, QStringLiteral("forward"), QStringLiteral("H"));
    QVERIFY(arrows(both).join(QLatin1Char(' ')).contains(QStringLiteral("H*")));  // also with an old plan
    if (const QByteArray dir = qgetenv("NFCAD_SNAPSHOT_DIR"); !dir.isEmpty()) {
      e.resize(1400, 700);
      e.highlightBranch(QStringLiteral("measurement"));
      e.scene()->clearSelection();
      for (const char* id : {"Beam_splitter", "Objective_A", "Sample", "Objective_B"}) e.nodeItem(QString::fromLatin1(id))->setSelected(true);
      e.zoomSelected();
      e.grab().toImage().save(QString::fromLocal8Bit(dir) + QStringLiteral("/node_editor_directions.png"));
    }
  }

  void connectionsAndTransports() {
    NodeEditor e;
    e.setWorkflow(model_linnik());
    QSignalSpy edited(&e, &NodeEditor::edited);
    QVERIFY(!e.addConnection(QStringLiteral("Objective_A"), QStringLiteral("Sample"), QStringLiteral("Reference"),
                             QStringLiteral("Objective_B")));  // both ports are taken
    e.removeConnection(3);
    QCOMPARE(e.connectionCount(), 8);
    QVERIFY(e.addConnection(QStringLiteral("Objective_A"), QStringLiteral("Sample"), QStringLiteral("Sample"),
                            QStringLiteral("Objective_A")));
    QCOMPARE(e.connectionCount(), 9);
    const int last = 8;
    e.setConnectionTransport(last, QStringLiteral("forward"), QStringLiteral("H"));
    QJsonObject t = e.workflow()["model"].toObject()["connections"].toArray()[last].toObject()["transport"].toObject();
    QCOMPARE(t["forward"].toString(), QStringLiteral("H"));
    QCOMPARE(t["back"].toString(), QStringLiteral("auto"));
    e.setConnectionTransport(last, QStringLiteral("both"), QStringLiteral("auto"));
    QVERIFY(!e.workflow()["model"].toObject()["connections"].toArray()[last].toObject().contains("transport"));
    QVERIFY(edited.count() >= 4);
  }

  void branchesHighlightAndRecord() {
    NodeEditor e;
    e.setWorkflow(model_linnik());
    e.highlightBranch(QStringLiteral("reference"));
    QCOMPARE(e.nodeItem(QStringLiteral("Objective_B"))->pen().widthF(), 3.0);
    QVERIFY(e.nodeItem(QStringLiteral("Objective_A"))->pen().widthF() < 3.0);  // not on it
    // record the reference arm to the pupil camera: click the port each block is left by
    e.beginBranch();
    QVERIFY(!e.recordPort(QStringLiteral("Beam_splitter"), QStringLiteral("Objective_B")));  // not a source
    QVERIFY(e.recordPort(QStringLiteral("source"), QStringLiteral("Illumination_stop")));
    QVERIFY(e.recordPort(QStringLiteral("Illumination_stop"), QStringLiteral("Beam_splitter")));
    QVERIFY(!e.recordPort(QStringLiteral("Objective_B"), QStringLiteral("Reference")));  // light is at the splitter
    QVERIFY(e.recordPort(QStringLiteral("Beam_splitter"), QStringLiteral("Objective_B")));
    QVERIFY(e.recordPort(QStringLiteral("Objective_B"), QStringLiteral("Reference")));
    QVERIFY(e.recordPort(QStringLiteral("Reference"), QStringLiteral("Objective_B")));
    QVERIFY(e.recordPort(QStringLiteral("Objective_B"), QStringLiteral("Beam_splitter")));
    QVERIFY(e.recordPort(QStringLiteral("Beam_splitter"), QStringLiteral("Pupil_camera")));
    QVERIFY(!e.branchComplete());
    QSignalSpy recorded(&e, &NodeEditor::branchRecorded);
    QVERIFY(e.recordDetector(QStringLiteral("Pupil_camera")));
    QCOMPARE(recorded.count(), 1);
    QVERIFY(!e.finishBranch(QStringLiteral("reference")));  // the name is taken
    QVERIFY(e.finishBranch(QStringLiteral("reference_pupil")));
    QVERIFY(e.branchNames().contains(QStringLiteral("reference_pupil")));
    const QJsonArray steps = e.workflow()["model"].toObject()["branches"].toArray().last().toObject()["steps"].toArray();
    QCOMPARE(steps.size(), 8);
    QCOMPARE(steps[4].toArray(), (QJsonArray{"Reference", "Objective_B", "Objective_B"}));  // the mirror
    QVERIFY(steps.last().toArray().at(2).isNull());
    e.removeBranch(QStringLiteral("measurement"));
    QVERIFY(!e.branchNames().contains(QStringLiteral("measurement")));
    // Escape cancels a recording
    e.beginBranch();
    QVERIFY(e.recordingBranch());
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&e, &esc);
    QVERIFY(!e.recordingBranch());
  }

  void branchPanel() {
    NodeEditor e;
    nfcad::ui::BranchPanel panel(&e);
    panel.ask_name = [](const QString& suggestion) { return suggestion; };
    e.setWorkflow(model_linnik());
    QCOMPARE(panel.list()->count(), 3);
    panel.list()->setCurrentRow(1);
    QCOMPARE(e.highlightedBranch(), QStringLiteral("reference"));
    e.beginBranch();
    for (const auto& [n, p] : std::initializer_list<std::pair<const char*, const char*>>{
             {"source", "Illumination_stop"}, {"Illumination_stop", "Beam_splitter"},
             {"Beam_splitter", "Objective_A"}, {"Objective_A", "Sample"}, {"Sample", "Objective_A"},
             {"Objective_A", "Beam_splitter"}, {"Beam_splitter", "Pupil_camera"}}) {
      QVERIFY(e.recordPort(QString::fromLatin1(n), QString::fromLatin1(p)));
    }
    QVERIFY(e.recordDetector(QStringLiteral("Pupil_camera")));  // the panel names it "branch 1"
    QCOMPARE(panel.list()->count(), 4);
    QVERIFY(e.branchNames().contains(QStringLiteral("branch 1")));
  }

  void modelUpToDateMarks() {
    NodeEditor e;
    e.setWorkflow(model_linnik());
    QJsonObject all;
    for (const QJsonValue& v : e.workflow()["model"].toObject()["nodes"].toArray()) all[v.toObject()["id"].toString()] = true;
    e.setStates(all);
    e.setParams(QStringLiteral("Sample"), QJsonObject{{"offset", QJsonArray{0, 0, 0.001}}});
    for (const char* stale : {"Sample", "Objective_A", "Beam_splitter", "Camera", "Pupil_camera"}) {
      QCOMPARE(e.state(QString::fromLatin1(stale)), 0);
    }
    QCOMPARE(e.state(QStringLiteral("source")), 1);
    QCOMPARE(e.state(QStringLiteral("Illumination_stop")), 1);
  }

  void modelReplaceKeepsPorts() {
    NodeEditor e;
    e.setWorkflow(model_linnik());
    if (!e.replacementsFor(QStringLiteral("Sample")).contains(QStringLiteral("fusion.grating"))) {
      QSKIP("the block library of the fixture has no grating");
    }
    QVERIFY(e.replaceNode(QStringLiteral("Sample"), QStringLiteral("fusion.grating")));
    const QJsonObject m = e.workflow()["model"].toObject();
    bool connected = false;
    for (const QJsonValue& v : m["connections"].toArray()) {
      const QJsonArray ends = v.toObject()["ports"].toArray();
      connected = connected || ends.contains(QJsonArray{"Sample", "front"});
    }
    QVERIFY(connected);  // the objective's connection moved to the grating's port
    for (const QJsonValue& b : m["branches"].toArray()) {
      for (const QJsonValue& s : b.toObject()["steps"].toArray()) {
        if (s.toArray().at(0).toString() == QStringLiteral("Sample")) {
          QCOMPARE(s.toArray(), (QJsonArray{"Sample", "front", "front"}));
        }
      }
    }
  }

  void fieldsOfAPluginNode() {
    // ADR Nachtrag 1: the plugin describes the data of a node (JSON pointers into its params);
    // the dialog edits them in a tab per element and writes back only what was changed
    const QString radius = QStringLiteral("/rtt/~1root~1children~11/surfaces/0/shape/base/radius/value");
    const QString coeff = QStringLiteral("/rtt/~1root~1children~11/surfaces/0/shape/base/coefficients/0");
    QJsonObject lens{{"type", "lens"},
                     {"name", "L1"},
                     {"surfaces", QJsonArray{QJsonObject{{"id", "S1"},
                                                         {"shape", QJsonObject{{"base", QJsonObject{{"type", "even_asphere"},
                                                                                                    {"radius", QJsonObject{{"value", 30.0}, {"variable", true}}},
                                                                                                    {"coefficients", QJsonArray{1.2345678901234567e-06}}}}}}}}}};
    QJsonObject node{{"id", "L"},
                     {"type", "rtt.section"},
                     {"params", QJsonObject{{"rtt", QJsonObject{{"/root/children/1", lens}}}}},
                     {"fields", QJsonArray{QJsonObject{{"name", radius}, {"type", "number"}, {"label", "S1 radius"}, {"group", "L1"}, {"unit", "mm"}},
                                           QJsonObject{{"name", coeff}, {"type", "number"}, {"label", "S1 a4"}, {"group", "L1"}}}}};
    QJsonObject model{{"format", "rtt-fusion-graph"}, {"version", 2}, {"nodes", QJsonArray{node}},
                      {"connections", QJsonArray{}}, {"links", QJsonArray{}}, {"branches", QJsonArray{}}};
    NodeEditor e;
    e.setWorkflow(QJsonObject{{"model", model}});
    auto value = [&e](const QString& pointer) {
      QJsonValue v = e.workflow()["model"].toObject()["nodes"].toArray()[0].toObject()["params"];
      for (QString part : pointer.mid(1).split(QLatin1Char('/'))) {
        part.replace(QStringLiteral("~1"), QStringLiteral("/"));
        v = v.isArray() ? v.toArray().at(part.toInt()) : v.toObject()[part];
      }
      return v;
    };
    e.setParams(QStringLiteral("L"), QJsonObject{{radius, 31.5}});
    QCOMPARE(value(radius).toDouble(), 31.5);
    QCOMPARE(value(radius.chopped(5) + QStringLiteral("variable")).toBool(), true);  // the rest stays
    QTimer::singleShot(0, &e, [radius] {
      for (QWidget* w : QApplication::topLevelWidgets()) {
        auto* d = qobject_cast<QDialog*>(w);
        if (d == nullptr || !d->isVisible()) continue;
        QVERIFY(d->findChild<QTabWidget*>() != nullptr);  // a tab per element
        auto* line = d->findChild<QLineEdit*>(radius);
        QVERIFY(line != nullptr);
        line->setText(QStringLiteral("42"));
        d->accept();
      }
    });
    e.editParams(QStringLiteral("L"));
    QCOMPARE(value(radius).toDouble(), 42.0);
    QCOMPARE(value(coeff).toDouble(), 1.2345678901234567e-06);  // shown with 12 digits, not rewritten
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

  void navigation() {
    // as in the CAD views: zoom to the cursor, pan by scroll and drag, zoom extents
    NodeEditor e;
    e.resize(800, 400);
    e.setAttribute(Qt::WA_DontShowOnScreen);
    e.setWorkflow(linnik());
    e.show();
    e.fitAll();
    const QPoint cursor(200, 150);
    const QPointF under = e.mapToScene(cursor);
    const double z0 = e.zoom();
    // mouse wheel one notch up: closer, the scene point under the cursor stays there
    QWheelEvent wheel(QPointF(cursor), QPointF(e.viewport()->mapToGlobal(cursor)), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(e.viewport(), &wheel);
    QVERIFY(e.zoom() > z0 * 1.1);
    QVERIFY(QLineF(e.mapToScene(cursor), under).length() < 2.0 / e.zoom());
    // zoom is limited
    for (int k = 0; k < 60; ++k) e.zoomAt(QPointF(cursor), 2.0);
    QVERIFY(e.zoom() <= 8.0 + 1e-9);
    // two-finger trackpad scroll pans (no zoom)
    const double z1 = e.zoom();
    const QPointF before = e.mapToScene(QPoint(400, 200));
    QWheelEvent scroll(QPointF(cursor), QPointF(e.viewport()->mapToGlobal(cursor)), QPoint(40, 0), QPoint(0, 0),
                       Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QApplication::sendEvent(e.viewport(), &scroll);
    QCOMPARE(e.zoom(), z1);
    QVERIFY(std::abs((before - e.mapToScene(QPoint(400, 200))).x() - 40.0 / z1) < 2.0 / z1);
    // a right drag pans far beyond the blocks (open canvas) and opens no menu
    const QPointF b2 = e.mapToScene(QPoint(400, 200));
    QTest::mousePress(e.viewport(), Qt::RightButton, {}, QPoint(400, 200));
    for (int x = 410; x <= 700; x += 10) {
      QMouseEvent move(QEvent::MouseMove, QPointF(x, 200), QPointF(e.viewport()->mapToGlobal(QPoint(x, 200))),
                       Qt::NoButton, Qt::RightButton, Qt::NoModifier);
      QApplication::sendEvent(e.viewport(), &move);
    }
    QTest::mouseRelease(e.viewport(), Qt::RightButton, {}, QPoint(700, 200));
    QVERIFY(std::abs((b2 - e.mapToScene(QPoint(400, 200))).x() - 300.0 / e.zoom()) < 2.0 / e.zoom());
    // zoom extents with Z E
    QTest::keyClick(&e, Qt::Key_Z);
    QTest::keyClick(&e, Qt::Key_E);
    QVERIFY(std::abs(e.zoom() - z0) < 1e-6);
  }

  void zoomSelected() {
    NodeEditor e;
    e.resize(800, 400);
    e.setAttribute(Qt::WA_DontShowOnScreen);
    e.setWorkflow(linnik());
    e.show();
    e.fitAll();
    const double all = e.zoom();
    // without a selection: all blocks
    e.zoomSelected();
    QVERIFY(std::abs(e.zoom() - all) < 1e-9);
    // one block selected: it fills the view, centred
    QGraphicsRectItem* sample = e.nodeItem(QStringLiteral("Sample"));
    sample->setSelected(true);
    QTest::keyClick(&e, Qt::Key_Z);
    QTest::keyClick(&e, Qt::Key_S);
    QVERIFY(e.zoom() > 2.0 * all);
    const QPoint centre = e.mapFromScene(sample->sceneBoundingRect().center());
    QVERIFY(std::abs(centre.x() - e.viewport()->width() / 2) < 3);
    QVERIFY(std::abs(centre.y() - e.viewport()->height() / 2) < 3);
    const QRect r = e.mapFromScene(sample->sceneBoundingRect()).boundingRect();
    QVERIFY(e.viewport()->rect().contains(r));
    // the editor takes Z, E and S before the main window's shortcuts
    QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_Z, Qt::NoModifier);
    QApplication::sendEvent(&e, &override);
    QVERIFY(override.isAccepted());
  }

  void snapshot() {
    NodeEditor e;
    e.resize(1500, 420);
    QJsonObject wf = linnik();
    // optional: the workflow of a document (.rttf) for a look at a real case
    if (const QByteArray doc = qgetenv("NFCAD_SNAPSHOT_WORKFLOW_DOC"); !doc.isEmpty()) {
      QFile f(QString::fromLocal8Bit(doc));
      if (f.open(QIODevice::ReadOnly)) wf = QJsonDocument::fromJson(f.readAll()).object()["workflow"].toObject();
      e.resize(1800, 700);
    }
    e.setWorkflow(wf);
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
