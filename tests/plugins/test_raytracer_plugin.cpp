// The Raytracer plugin (python/plugins/raytracer, P6) in a real plugin process against a real
// document: load, trace (rays + irradiance), optimise (live geometry), undo. Needs a Python with
// the packages numpy and raytatouille (NFCAD_TEST_PYTHON, else the repository's .venv); skipped
// otherwise – CI has no Raytatouille build.
#include <QDir>
#include <algorithm>
#include <QJsonArray>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

#include "document/document.hpp"
#include "document/selection.hpp"
#include "plugins/plugin_host.hpp"
#include "plugins/plugin_manager.hpp"
#include "render/scene.hpp"
#include "results/result_store.hpp"
#include "tessellation/tessellation_service.hpp"
#include "viewport/viewport.hpp"

using namespace nfcad;

namespace {
QString python_with_raytatouille() {
  QStringList candidates;
  if (const QString env = qEnvironmentVariable("NFCAD_TEST_PYTHON"); !env.isEmpty()) candidates << env;
  candidates << QStringLiteral(NFCAD_SOURCE_DIR "/.venv/bin/python")
             << QStringLiteral(NFCAD_SOURCE_DIR "/.venv/Scripts/python.exe");
  for (const QString& p : candidates) {
    if (!QFileInfo::exists(p)) continue;
    QProcess probe;
    probe.start(p, {QStringLiteral("-c"), QStringLiteral("import numpy, raytatouille")});
    if (probe.waitForFinished(30000) && probe.exitCode() == 0) return p;
  }
  return {};
}

const document::Entity* by_name(const document::Document& doc, const QString& name) {
  for (const auto& [id, e] : doc.entities()) {
    if (e.name == name) return &e;
  }
  return nullptr;
}
}  // namespace

class TestRaytracerPlugin : public QObject {
  Q_OBJECT
  document::Document doc_;
  document::Selection selection_;
  results::ResultStore results_;
  std::unique_ptr<plugins::PluginHost> host_;
  QJsonObject params_;
  QString python_;

  void run(const QString& action) {
    QSignalSpy finished(host_.get(), &plugins::PluginHost::finished);
    QVERIFY(host_->run(action, params_) != 0);
    QVERIFY(finished.wait(120000));
    const auto args = finished.first();
    QVERIFY2(args.at(1).toBool(), qPrintable(action + QStringLiteral(": ") + args.at(3).toString()));
    qInfo().noquote() << action << "->" << args.at(3).toString();
  }

 private slots:
  void initTestCase() {
    const QString python = python_with_raytatouille();
    python_ = python;
    if (python.isEmpty()) QSKIP("no Python with raytatouille (pip install <Raytatouille> into .venv)");
    selection_.attach(&doc_);
    const QString dir = QStringLiteral(NFCAD_SOURCE_DIR "/python/plugins/raytracer");
    QString error;
    const auto m = plugins::read_manifest(dir, &error);
    QVERIFY2(m.has_value(), qPrintable(error));
    host_ = std::make_unique<plugins::PluginHost>(*m, &doc_, &selection_, &results_);
    QSignalSpy ready(host_.get(), &plugins::PluginHost::readyChanged);
    host_->start(python, plugins::PluginManager::nfcadPythonPath());
    QVERIFY(ready.wait(30000));
    params_ = {{"system", dir + QStringLiteral("/examples/singlet_optim.rtt.json")},
               {"irradiance_rays", 469},
               {"grid", 64}};
  }

  void load() {
    run(QStringLiteral("load"));
    for (const char* name : {"STO", "L1.S1", "L1.S2", "IMG", "L1 edge"}) {
      QVERIFY2(by_name(doc_, QString::fromLatin1(name)) != nullptr, name);
    }
    QCOMPARE(doc_.entities().size(), std::size_t{5});
    QCOMPARE(doc_.groups().size(), std::size_t{4});  // the system + stop, L1, image
    const document::Entity* s1 = by_name(doc_, QStringLiteral("L1.S1"));
    QCOMPARE(doc_.group(s1->group)->name, QStringLiteral("L1"));
    QCOMPARE(s1->attributes.value(QStringLiteral("rtt.kind")).toString(), QStringLiteral("lens"));
    // loading again replaces the drawing
    run(QStringLiteral("load"));
    QCOMPARE(doc_.entities().size(), std::size_t{5});
    QCOMPARE(doc_.groups().size(), std::size_t{4});
    doc_.undoStack()->undo();  // back to the first drawing
    QCOMPARE(doc_.entities().size(), std::size_t{5});
    QCOMPARE(doc_.groups().size(), std::size_t{4});
    doc_.undoStack()->redo();
  }

  void trace() {
    run(QStringLiteral("trace"));
    QVERIFY(!results_.sets().empty());
    auto items = results_.sets().back().items;
    QCOMPARE(items.size(), std::size_t{1});  // rays of the one path; the quick irradiance is off
    QCOMPARE(items[0].kind, results::ResultItem::Kind::Lines);
    params_.insert(QStringLiteral("irradiance"), true);
    run(QStringLiteral("trace"));
    items = results_.sets().back().items;
    QCOMPARE(items.size(), std::size_t{2});
    QCOMPARE(items[1].kind, results::ResultItem::Kind::Field);
    params_.remove(QStringLiteral("irradiance"));
  }

  void cameraImage() {
    params_.insert(QStringLiteral("camera_rays"), 30000);
    params_.insert(QStringLiteral("camera_pixels"), 80);
    run(QStringLiteral("camera_image"));
    const auto& items = results_.sets().back().items;
    QCOMPARE(items.size(), std::size_t{2});  // the image, and the field on the surface
    QCOMPARE(items[0].kind, results::ResultItem::Kind::Image);
    QCOMPARE(items[1].kind, results::ResultItem::Kind::Field);
    const results::Image& im = *items[0].image;
    QCOMPARE(im.width, 80);  // square image surface: 80 x 80
    QCOMPARE(im.height, 80);
    QVERIFY(im.x1 > im.x0);
    // the on-axis field of the example: a symmetric spot, its centroid in the image centre
    // (the brightest pixel may sit on a caustic ring, so not the peak)
    double sum = 0, cx = 0, cy = 0;
    for (int j = 0; j < im.height; ++j) {
      for (int i = 0; i < im.width; ++i) {
        const double v = im.value(i, j);
        sum += v;
        cx += v * (i + 0.5);
        cy += v * (j + 0.5);
      }
    }
    QVERIFY(sum > 0);
    QVERIFY2(std::abs(cx / sum - im.width / 2.0) < 0.1 && std::abs(cy / sum - im.height / 2.0) < 0.1,
             qPrintable(QStringLiteral("centroid %1 %2").arg(cx / sum).arg(cy / sum)));
    QVERIFY(im.description.contains(QStringLiteral("rays")));
  }

  void optimiseAndUndo() {
    const document::Entity* s1 = by_name(doc_, QStringLiteral("L1.S1"));
    const document::EntityId id = s1->id;
    const document::GeometryPtr before = s1->geometry;
    run(QStringLiteral("optimise"));
    QVERIFY(doc_.entity(id)->geometry != before);  // redrawn
    const document::Entity* sto = by_name(doc_, QStringLiteral("STO"));
    const int optimised = sto->attributes.value(QStringLiteral("rtt.revision")).toInt();
    QVERIFY(optimised > 1);
    QVERIFY(!results_.sets().back().items.empty());  // the merit curve
    doc_.undoStack()->undo();
    QVERIFY(doc_.entity(id)->geometry == before);
    QVERIFY(by_name(doc_, QStringLiteral("STO"))->attributes.value(QStringLiteral("rtt.revision")).toInt() < optimised);
    doc_.undoStack()->redo();
    QCOMPARE(by_name(doc_, QStringLiteral("STO"))->attributes.value(QStringLiteral("rtt.revision")).toInt(), optimised);
    run(QStringLiteral("trace"));  // with the optimised system
  }

  /// Attaches a display block to an output of the workflow in the document.
  /// The part of the workflow with the blocks: the model (RTT-Fusion R24) or the socket graph.
  QString part() const { return doc_.workflow().contains("model") ? QStringLiteral("model") : QStringLiteral("graph"); }

  void attachDisplay(const QString& id, const QString& type, const QString& from, const QString& socket) {
    QJsonObject wf = doc_.workflow();
    QJsonObject graph = wf[part()].toObject();
    QJsonArray nodes = graph["nodes"].toArray();
    nodes.append(QJsonObject{{"id", id}, {"type", type}});
    graph["nodes"] = nodes;
    QJsonArray links = graph["links"].toArray();
    links.append(QJsonObject{{"from", QJsonArray{from, socket}}, {"to", QJsonArray{id, QStringLiteral("in")}}});
    graph["links"] = links;
    wf[part()] = graph;
    doc_.setWorkflow(wf);
  }

  QString detectorId() const {
    for (const QJsonValue& n : doc_.workflow()[part()].toObject()["nodes"].toArray()) {
      if (n.toObject()["type"].toString() == QStringLiteral("rtt.detector")) return n.toObject()["id"].toString();
    }
    return {};
  }

  void workflow() {
    // RTT-Fusion node workflow: graph from the system's paths, then a coherent camera image
    QProcess probe;
    probe.start(python_, {QStringLiteral("-c"), QStringLiteral("import rtt_fusion.graph")});
    if (!probe.waitForFinished(30000) || probe.exitCode() != 0) QSKIP("rtt_fusion not installed in the plugin Python");
    doc_.setWorkflow(QJsonObject{});  // a fresh import (the action keeps edits of an old one)
    run(QStringLiteral("workflow_from_system"));
    const QJsonObject wf = doc_.workflow();
    const QJsonArray nodes = wf[part()].toObject()["nodes"].toArray();
    QVERIFY(nodes.size() >= 3);  // source, sections, detector
    QProcess has_model;
    has_model.start(python_, {QStringLiteral("-c"), QStringLiteral("from rtt_fusion.graph import model_from_raytatouille")});
    if (has_model.waitForFinished(30000) && has_model.exitCode() == 0) QCOMPARE(part(), QStringLiteral("model"));
    if (part() == QStringLiteral("model")) {  // RTT-Fusion R24: the system's paths are the branches
      QVERIFY(!wf["model"].toObject()["branches"].toArray().isEmpty());
      QVERIFY(!wf["model"].toObject()["connections"].toArray().isEmpty());
      // ADR Nachtrag 1: the data of the system are in the nodes, the plugin describes the fields
      bool system = false, element = false;
      for (const QJsonValue& n : nodes) {
        for (const QJsonValue& f : n.toObject()["fields"].toArray()) {
          system = system || f.toObject()["group"].toString() == QStringLiteral("System");
          element = element || (f.toObject()["name"].toString().startsWith(QStringLiteral("/rtt/")) &&
                                f.toObject()["unit"].toString() == QStringLiteral("mm"));
        }
      }
      QProcess has_data;
      has_data.start(python_, {QStringLiteral("-c"), QStringLiteral("from rtt_fusion.graph.importer import system_from_model")});
      if (has_data.waitForFinished(30000) && has_data.exitCode() == 0) {
        QVERIFY(system);
        QVERIFY(element);
      }
    }
    QVERIFY(!wf["types"].toArray().isEmpty());
    QProcess has_grating;
    has_grating.start(python_, {QStringLiteral("-c"), QStringLiteral("import rtt_fusion.graph.grating")});
    if (has_grating.waitForFinished(30000) && has_grating.exitCode() == 0) {
      bool grating = false;  // RTT-Fusion R19: the grating sample is in the block library
      for (const QJsonValue& t : wf["types"].toArray()) grating = grating || t.toObject()["type"].toString() == QStringLiteral("fusion.grating");
      QVERIFY(grating);
    }
    QProcess has_particle;
    has_particle.start(python_, {QStringLiteral("-c"), QStringLiteral("import rtt_fusion.graph.particle")});
    if (has_particle.waitForFinished(30000) && has_particle.exitCode() == 0) {
      bool particle = false;  // RTT-Fusion R19c: the particle sample (CliffordBem)
      for (const QJsonValue& t : wf["types"].toArray()) particle = particle || t.toObject()["type"].toString() == QStringLiteral("fusion.particle");
      QVERIFY(particle);
    }
    bool detector = false;
    for (const QJsonValue& n : nodes) detector = detector || n.toObject()["type"].toString() == QStringLiteral("rtt.detector");
    QVERIFY(detector);
    params_.insert(QStringLiteral("workflow_rays"), 2000);
    params_.insert(QStringLiteral("workflow_pixels"), 64);
    QVERIFY(!doc_.workflow()["plan"].toObject()["links"].toObject().isEmpty());  // R17 planner
    run(QStringLiteral("check_workflow"));
    for (const QJsonValue& v : doc_.workflow()["plan"].toObject()["links"].toObject()) {
      QCOMPARE(v.toObject()["representation"].toString(), QStringLiteral("R"));  // "auto": rays
    }
    // only what a display block shows is computed (no automatic camera images)
    const std::size_t sets_before = results_.sets().size();
    QSignalSpy finished(host_.get(), &plugins::PluginHost::finished);
    QVERIFY(host_->run(QStringLiteral("run_workflow"), params_) != 0);
    QVERIFY(finished.wait(60000));
    QVERIFY(!finished.at(0).at(1).toBool());
    QVERIFY(finished.at(0).at(3).toString().contains(QStringLiteral("attach a display block")));
    QVERIFY(results_.sets().size() <= sets_before + 1);
    attachDisplay(QStringLiteral("show"), QStringLiteral("display.image"), detectorId(), QStringLiteral("image"));
    run(QStringLiteral("run_workflow"));
    const auto& items = results_.sets().back().items;
    QCOMPARE(items.size(), std::size_t{1});
    QCOMPARE(items[0].kind, results::ResultItem::Kind::Image);
    QCOMPARE(items[0].key, QStringLiteral("workflow:show"));

    // field tracing (RTT-Fusion R18b): the link into the detector as H, a small window around
    // the focus – the rays meet in less than a pixel, the field shows the focal spot
    QProcess ft;
    ft.start(python_, {QStringLiteral("-c"), QStringLiteral("import fieldtracing")});
    if (!ft.waitForFinished(30000) || ft.exitCode() != 0) QSKIP("fieldtracing not installed in the plugin Python");
    QJsonObject fwf = doc_.workflow();
    QJsonObject graph = fwf[part()].toObject();
    const QString det_id = detectorId();
    if (part() == QStringLiteral("model")) {  // the connection into the camera carries fields
      QJsonArray connections = graph["connections"].toArray();
      for (int k = 0; k < connections.size(); ++k) {
        QJsonObject c = connections[k].toObject();
        for (const QJsonValue& e : c["ports"].toArray()) {
          if (e.toArray()[0].toString() == det_id) c["transport"] = QStringLiteral("H");
        }
        connections[k] = c;
      }
      graph["connections"] = connections;
    } else {
      QJsonArray links = graph["links"].toArray();
      for (int k = 0; k < links.size(); ++k) {
        QJsonObject l = links[k].toObject();
        if (l["to"].toArray()[0].toString() == det_id) l["transport"] = QStringLiteral("H");
        links[k] = l;
      }
      graph["links"] = links;
    }
    fwf[part()] = graph;
    doc_.setWorkflow(fwf);
    params_.insert(QStringLiteral("workflow_window"), 0.02);
    params_.insert(QStringLiteral("workflow_pixels"), 32);
    run(QStringLiteral("check_workflow"));
    bool planned_h = false;
    for (const QJsonValue& v : doc_.workflow()["plan"].toObject()["links"].toObject()) {
      planned_h = planned_h || v.toObject()["representation"].toString() == QStringLiteral("H");
    }
    QVERIFY(planned_h);
    run(QStringLiteral("run_workflow"));
    const auto& field_items = results_.sets().back().items;
    QVERIFY(!field_items.empty());
    QCOMPARE(field_items[0].key, QStringLiteral("workflow:show"));
    const auto& values = field_items[0].image->values;
    QVERIFY(*std::max_element(values.begin(), values.end()) > 0.0);
  }

  void workflowKeepsEdits() {
    // "Workflow from system" again keeps the user's transports, added blocks and branches
    QProcess probe;
    probe.start(python_, {QStringLiteral("-c"), QStringLiteral("import rtt_fusion.graph")});
    if (!probe.waitForFinished(30000) || probe.exitCode() != 0) QSKIP("rtt_fusion not installed in the plugin Python");
    doc_.setWorkflow(QJsonObject{});  // a fresh import (the action keeps edits of an old one)
    run(QStringLiteral("workflow_from_system"));
    QJsonObject wf = doc_.workflow();
    QJsonObject graph = wf[part()].toObject();
    const bool model = part() == QStringLiteral("model");
    const QString list = model ? QStringLiteral("connections") : QStringLiteral("links");
    QJsonArray items = graph[list].toArray();
    QJsonObject last = items.last().toObject();
    last["transport"] = QStringLiteral("H");
    items[items.size() - 1] = last;
    graph[list] = items;
    QJsonArray nodes = graph["nodes"].toArray();
    nodes.append(QJsonObject{{"id", "my_sweep"}, {"type", "meta.sweep"}, {"params", QJsonObject{{"values", QJsonArray{1.0}}}}});
    graph["nodes"] = nodes;
    QJsonObject mine;
    if (model) {  // a branch of the user's: a copy of the first under another name
      QJsonArray branches = graph["branches"].toArray();
      mine = branches.first().toObject();
      mine["name"] = QStringLiteral("my branch");
      branches.append(mine);
      graph["branches"] = branches;
    }
    wf[part()] = graph;
    doc_.setWorkflow(wf);
    run(QStringLiteral("workflow_from_system"));
    const QJsonObject again = doc_.workflow()[part()].toObject();
    bool sweep = false;
    for (const QJsonValue& n : again["nodes"].toArray()) sweep = sweep || n.toObject()["id"].toString() == QStringLiteral("my_sweep");
    QVERIFY(sweep);
    bool fixed = false;
    for (const QJsonValue& l : again[list].toArray()) {
      const QJsonObject o = l.toObject();
      const bool same = model ? o["ports"] == last["ports"] : (o["from"] == last["from"] && o["to"] == last["to"]);
      fixed = fixed || (same && o["transport"].toString() == QStringLiteral("H"));
    }
    QVERIFY(fixed);
    if (model) QVERIFY(again["branches"].toArray().contains(mine));
  }

  void workflowDisplays() {
    // RTT-Fusion R21: display blocks give results with the key "workflow:<block>"
    QProcess probe;
    probe.start(python_, {QStringLiteral("-c"), QStringLiteral("import rtt_fusion.graph.display")});
    if (!probe.waitForFinished(30000) || probe.exitCode() != 0) QSKIP("rtt_fusion without display blocks");
    doc_.setWorkflow(QJsonObject{});
    run(QStringLiteral("workflow_from_system"));
    QJsonObject wf = doc_.workflow();
    QJsonObject graph = wf[part()].toObject();
    QString camera, section, section_out;
    for (const QJsonValue& n : graph["nodes"].toArray()) {
      const QJsonObject o = n.toObject();
      if (o["type"].toString() == QStringLiteral("rtt.detector")) camera = o["id"].toString();
      if (o["type"].toString() == QStringLiteral("rtt.section") && section.isEmpty()) {
        section = o["id"].toString();
        for (const QJsonValue& sk : o["sockets"].toArray()) {
          if (sk.toObject()["direction"].toString() == QStringLiteral("out") && section_out.isEmpty()) section_out = sk.toObject()["name"].toString();
        }
      }
    }
    QVERIFY(!camera.isEmpty() && !section.isEmpty() && !section_out.isEmpty());
    QJsonArray nodes = graph["nodes"].toArray();
    nodes.append(QJsonObject{{"id", "show"}, {"type", "display.image"}, {"name", "Camera window"}});
    graph["nodes"] = nodes;
    QJsonArray links = graph["links"].toArray();
    links.append(QJsonObject{{"from", QJsonArray{camera, QStringLiteral("image")}}, {"to", QJsonArray{"show", "in"}}});
    graph["links"] = links;
    wf[part()] = graph;
    doc_.setWorkflow(wf);
    params_.insert(QStringLiteral("workflow_rays"), 500);
    params_.insert(QStringLiteral("workflow_pixels"), 32);
    params_.insert(QStringLiteral("workflow_window"), 0.0);
    run(QStringLiteral("run_workflow"));
    bool keyed = false;
    for (const auto& item : results_.sets().back().items) {
      keyed = keyed || (item.key == QStringLiteral("workflow:show") && item.kind == results::ResultItem::Kind::Image);
    }
    QVERIFY(keyed);
    QCOMPARE(results_.itemsWithKey(QStringLiteral("workflow:show"), results::ResultItem::Kind::Image).size(), std::size_t{1});
  }

  void workflowKeepsResults() {
    // RTT-Fusion R23: results kept across runs; the editor gets up to date / out of date marks
    QProcess probe;
    probe.start(python_, {QStringLiteral("-c"), QStringLiteral("from rtt_fusion.graph.execute import ResultCache")});
    if (!probe.waitForFinished(30000) || probe.exitCode() != 0) QSKIP("rtt_fusion without the result cache");
    doc_.setWorkflow(QJsonObject{});
    run(QStringLiteral("workflow_from_system"));
    attachDisplay(QStringLiteral("shown"), QStringLiteral("display.image"), detectorId(), QStringLiteral("image"));
    params_.insert(QStringLiteral("workflow_rays"), 500);
    params_.insert(QStringLiteral("workflow_pixels"), 32);
    params_.insert(QStringLiteral("workflow_window"), 0.0);
    QSignalSpy states(&doc_, &document::Document::workflowState);
    run(QStringLiteral("run_workflow"));
    QVERIFY(!states.isEmpty());
    QVERIFY(states.last().at(0).toJsonObject()["shown"].toBool());
    QVERIFY(states.last().at(0).toJsonObject()[detectorId()].toBool());
    // a change behind the camera: the display is out of date, the camera not
    QJsonObject wf = doc_.workflow();
    QJsonObject graph = wf[part()].toObject();
    QJsonArray nodes = graph["nodes"].toArray();
    for (int k = 0; k < nodes.size(); ++k) {
      QJsonObject n = nodes[k].toObject();
      if (n["id"].toString() == QStringLiteral("shown")) n["params"] = QJsonObject{{"title", "renamed"}};
      nodes[k] = n;
    }
    graph["nodes"] = nodes;
    wf[part()] = graph;
    doc_.setWorkflow(wf);
    states.clear();
    run(QStringLiteral("check_workflow"));
    QVERIFY(!states.isEmpty());
    QVERIFY(!states.last().at(0).toJsonObject()["shown"].toBool());
    QVERIFY(states.last().at(0).toJsonObject()[detectorId()].toBool());
  }

  void workflowSweep() {
    // RTT-Fusion R20: a meta block sweeps the offset of a block and collects the camera image
    QProcess probe;
    probe.start(python_, {QStringLiteral("-c"), QStringLiteral("import rtt_fusion.graph.meta")});
    if (!probe.waitForFinished(30000) || probe.exitCode() != 0) QSKIP("rtt_fusion without meta blocks");
    doc_.setWorkflow(QJsonObject{});  // a fresh import (the action keeps edits of an old one)
    run(QStringLiteral("workflow_from_system"));
    QJsonObject wf = doc_.workflow();
    QJsonObject graph = wf[part()].toObject();
    QString section;
    QString camera;
    for (const QJsonValue& n : graph["nodes"].toArray()) {
      const QJsonObject o = n.toObject();
      if (o["type"].toString() == QStringLiteral("rtt.section") && section.isEmpty()) section = o["id"].toString();
      if (o["type"].toString() == QStringLiteral("rtt.detector")) camera = o["id"].toString();
    }
    QVERIFY(!section.isEmpty() && !camera.isEmpty());
    QJsonArray nodes = graph["nodes"].toArray();
    nodes.append(QJsonObject{{"id", "sweep"},
                             {"type", "meta.sweep"},
                             {"params", QJsonObject{{"parameter", section + QStringLiteral(".offset[2]")},
                                                    {"collect", camera + QStringLiteral(".image")},
                                                    {"measure", "power"},
                                                    {"values", QJsonArray{0.0, 0.5, 1.0}}}}});
    graph["nodes"] = nodes;
    wf[part()] = graph;
    doc_.setWorkflow(wf);
    params_.insert(QStringLiteral("workflow_rays"), 500);
    params_.insert(QStringLiteral("workflow_pixels"), 32);
    params_.insert(QStringLiteral("workflow_window"), 0.0);
    attachDisplay(QStringLiteral("curve"), QStringLiteral("display.plot"), QStringLiteral("sweep"), QStringLiteral("results"));
    run(QStringLiteral("run_workflow"));
    const auto& items = results_.sets().back().items;
    bool curve = false;
    for (const auto& item : items) {
      if (item.kind == results::ResultItem::Kind::Curve && item.key == QStringLiteral("workflow:curve")) {
        curve = true;
        QCOMPARE(item.curve->x.size(), std::size_t{3});
      }
    }
    QVERIFY(curve);
  }

  /// The radius of L1.S1 in the workflow's data (ADR Nachtrag 1), and a setter.
  double workflowRadius() const {
    for (const QJsonValue& n : doc_.workflow()["model"].toObject()["nodes"].toArray()) {
      const QJsonObject rtt = n.toObject()["params"].toObject()["rtt"].toObject();
      for (auto it = rtt.begin(); it != rtt.end(); ++it) {
        if (it.value().toObject()["name"].toString() == QStringLiteral("L1")) {
          return it.value().toObject()["surfaces"].toArray()[0].toObject()["shape"].toObject()["base"].toObject()["radius"].toObject()["value"].toDouble();
        }
      }
    }
    return 0.0;
  }
  void setWorkflowRadius(double r) {
    QJsonObject wf = doc_.workflow();
    QJsonObject model = wf["model"].toObject();
    QJsonArray nodes = model["nodes"].toArray();
    for (int k = 0; k < nodes.size(); ++k) {
      QJsonObject n = nodes[k].toObject();
      QJsonObject params = n["params"].toObject();
      QJsonObject rtt = params["rtt"].toObject();
      for (auto it = rtt.begin(); it != rtt.end(); ++it) {
        QJsonObject e = it.value().toObject();
        if (e["name"].toString() != QStringLiteral("L1")) continue;
        QJsonArray surfaces = e["surfaces"].toArray();
        QJsonObject s1 = surfaces[0].toObject();
        QJsonObject shape = s1["shape"].toObject();
        QJsonObject base = shape["base"].toObject();
        QJsonObject radius = base["radius"].toObject();
        radius["value"] = r;
        base["radius"] = radius;
        shape["base"] = base;
        s1["shape"] = shape;
        surfaces[0] = s1;
        e["surfaces"] = surfaces;
        rtt[it.key()] = e;
      }
      params["rtt"] = rtt;
      n["params"] = params;
      nodes[k] = n;
    }
    model["nodes"] = nodes;
    wf["model"] = model;
    doc_.setWorkflow(wf);
  }
  const document::Entity* surface(const QString& id) const {
    for (const auto& [eid, e] : doc_.entities()) {
      if (e.attributes.value(QStringLiteral("rtt.surface")).toString() == id) return &e;
    }
    return nullptr;
  }

  void workflowIsTheTruth() {
    // ADR Nachtrag 1 / S3: the drawing, Optimise and Save follow the node workflow
    QProcess probe;
    probe.start(python_, {QStringLiteral("-c"), QStringLiteral("from rtt_fusion.graph.importer import system_from_graph")});
    if (!probe.waitForFinished(30000) || probe.exitCode() != 0) QSKIP("rtt_fusion without S2");
    doc_.setWorkflow(QJsonObject{});
    run(QStringLiteral("workflow_from_system"));
    run(QStringLiteral("update_drawing"));
    QVERIFY(surface(QStringLiteral("L1.S1")) != nullptr);
    const auto before = surface(QStringLiteral("L1.S1"))->geometry;
    QVERIFY(std::abs(workflowRadius() - 103.36) < 1e-9);
    // an edit in the workflow: the drawing is made again from the exact data – by the quiet
    // action CAD runs after every edit of the workflow
    bool follows = false;
    for (const auto& a : host_->actions()) follows = follows || (a.id == QStringLiteral("follow_workflow") && a.on_workflow_edit);
    QVERIFY(follows);
    setWorkflowRadius(60.0);
    run(QStringLiteral("follow_workflow"));
    QVERIFY(surface(QStringLiteral("L1.S1")) != nullptr);
    QVERIFY(surface(QStringLiteral("L1.S1"))->geometry != before);
    {  // a half-done edit (a radius of 0 is no surface) does not fail; the drawing waits
      const auto drawn = surface(QStringLiteral("L1.S1"))->geometry;
      setWorkflowRadius(0.0);
      run(QStringLiteral("follow_workflow"));
      QVERIFY(surface(QStringLiteral("L1.S1")) != nullptr);
      QVERIFY(surface(QStringLiteral("L1.S1"))->geometry == drawn);
      setWorkflowRadius(60.0);
    }
    // Optimise works on the workflow's system and writes its result back – one undo step
    const double edited = workflowRadius();
    run(QStringLiteral("optimise"));
    const double optimised = workflowRadius();
    QVERIFY(std::abs(optimised - edited) > 1e-6);
    doc_.undoStack()->undo();
    QVERIFY(std::abs(workflowRadius() - edited) < 1e-12);
    doc_.undoStack()->redo();
    QVERIFY(std::abs(workflowRadius() - optimised) < 1e-12);
    // S4: a part of the library has editable fields after the check
    QProcess has_parts;
    has_parts.start(python_, {QStringLiteral("-c"), QStringLiteral("import rtt_fusion.graph.parts")});
    if (has_parts.waitForFinished(30000) && has_parts.exitCode() == 0) {
      bool listed = false;
      for (const QJsonValue& t : doc_.workflow()["types"].toArray()) listed = listed || t.toObject()["type"].toString() == QStringLiteral("rtt.part.splitter");
      QVERIFY(listed);
      QJsonObject wf = doc_.workflow();
      QJsonObject model = wf["model"].toObject();
      QJsonArray nodes = model["nodes"].toArray();
      QJsonObject part{{"id", "new_bs"}, {"type", "rtt.part.splitter"}};
      for (const QJsonValue& t : wf["types"].toArray()) {
        if (t.toObject()["type"].toString() != QStringLiteral("rtt.part.splitter")) continue;
        for (const QJsonValue& p : t.toObject()["params"].toArray()) part["params"] = QJsonObject{{p.toObject()["name"].toString(), p.toObject()["default"]}};
      }
      nodes.append(part);
      model["nodes"] = nodes;
      wf["model"] = model;
      doc_.setWorkflow(wf);
      run(QStringLiteral("check_workflow"));  // the new part is drawn (a step of its own)
      QVERIFY(surface(QStringLiteral("new_bs.BS")) != nullptr);
      const int steps = doc_.undoStack()->count();
      run(QStringLiteral("check_workflow"));
      QCOMPARE(doc_.undoStack()->count(), steps);  // a check without a change: no undo step
      bool fields = false;
      for (const QJsonValue& n : doc_.workflow()["model"].toObject()["nodes"].toArray()) {
        if (n.toObject()["id"].toString() != QStringLiteral("new_bs")) continue;
        for (const QJsonValue& f : n.toObject()["fields"].toArray()) {
          fields = fields || f.toObject()["name"].toString().startsWith(QStringLiteral("/rtt_new/pose/rotation_deg"));
        }
      }
      QVERIFY(fields);
    }
    // Save system exports the workflow's system
    QTemporaryDir dir;
    params_.insert(QStringLiteral("save_as"), dir.filePath(QStringLiteral("export.rtt.json")));
    run(QStringLiteral("save"));
    params_.insert(QStringLiteral("save_as"), QString());
    QFile f(dir.filePath(QStringLiteral("export.rtt.json")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.readAll().contains(QByteArray::number(optimised, 'g', 10).left(6)));
  }

  void keptEditsKeepTheSystemData() {
    // Workflow from system over a workflow whose blocks lack the system's data (made before they
    // were kept, ADR Nachtrag 1): the edits stay, the data come from the new import
    QProcess probe;
    probe.start(python_, {QStringLiteral("-c"), QStringLiteral("from rtt_fusion.graph.importer import system_from_graph")});
    if (!probe.waitForFinished(30000) || probe.exitCode() != 0) QSKIP("rtt_fusion without S2");
    run(QStringLiteral("load"));
    doc_.setWorkflow(QJsonObject{});
    run(QStringLiteral("workflow_from_system"));
    QJsonObject wf = doc_.workflow();
    QJsonObject model = wf["model"].toObject();
    QJsonArray nodes = model["nodes"].toArray();
    int stripped = 0;
    for (int k = 0; k < nodes.size(); ++k) {
      QJsonObject n = nodes[k].toObject();
      QJsonObject params = n["params"].toObject();
      if (!params.contains("rtt")) continue;
      params.remove(QStringLiteral("rtt"));
      params.remove(QStringLiteral("runs"));
      n["params"] = params;
      n["position"] = QJsonArray{1234.0, 56.0};  // an edit
      nodes[k] = n;
      ++stripped;
    }
    QVERIFY(stripped > 0);
    model["nodes"] = nodes;
    wf["model"] = model;
    doc_.setWorkflow(wf);
    run(QStringLiteral("workflow_from_system"));
    int with_data = 0;
    for (const QJsonValue& v : doc_.workflow()["model"].toObject()["nodes"].toArray()) {
      const QJsonObject n = v.toObject();
      if (!n["params"].toObject().contains("rtt")) continue;
      ++with_data;
      QVERIFY2(n["params"].toObject().contains("runs"), qPrintable(n["id"].toString()));
      QCOMPARE(n["position"].toArray().at(0).toDouble(), 1234.0);  // the edit stays
    }
    QCOMPARE(with_data, stripped);
  }

  void save() {
    QTemporaryDir dir;
    params_.insert(QStringLiteral("save_as"), dir.filePath(QStringLiteral("out.rtt.json")));
    run(QStringLiteral("save"));
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("out.rtt.json"))));
  }

  void snapshot() {
    // rays and irradiance in a 3D view, offscreen; saved with NFCAD_SNAPSHOT_DIR
    tessellation::TessellationService service;
    render::Scene scene(&doc_, &service);
    QSignalSpy idle(&service, &tessellation::TessellationService::idle);
    if (idle.isEmpty()) idle.wait(20000);  // the scene tessellates the existing entities
    viewport::Viewport v(viewport::ViewKind::Perspective);
    v.resize(900, 600);
    v.setScene(&scene);
    v.setResults(&results_);
    v.zoomExtents();
    const QImage img = v.grabFramebuffer();
    if (img.isNull()) QSKIP("no RHI backend for offscreen rendering");
    const QByteArray dir = qgetenv("NFCAD_SNAPSHOT_DIR");
    if (!dir.isEmpty()) img.save(QString::fromLocal8Bit(dir) + QStringLiteral("/raytracer_plugin.png"));

    // the image surface alone, from above: the irradiance (inferno) must show a bright spot
    doc_.isolate({by_name(doc_, QStringLiteral("IMG"))->id});
    QTest::qWait(200);
    viewport::Viewport top(viewport::ViewKind::Top);
    top.resize(600, 600);
    top.setScene(&scene);
    top.setResults(&results_);
    top.zoomExtents();
    const QImage spot = top.grabFramebuffer();
    QVERIFY(!spot.isNull());
    int bright = 0;
    for (int y = 0; y < spot.height(); y += 2) {
      for (int x = 0; x < spot.width(); x += 2) {
        const QColor c = spot.pixelColor(x, y);
        bright += (c.red() > 200 && c.green() > 120) ? 1 : 0;  // the yellow end of inferno
      }
    }
    QVERIFY2(bright > 0, "no bright irradiance on the image surface");
    if (!dir.isEmpty()) spot.save(QString::fromLocal8Bit(dir) + QStringLiteral("/raytracer_irradiance.png"));
  }

  void cleanupTestCase() {
    if (host_) host_->stop();
  }
};

QTEST_MAIN(TestRaytracerPlugin)
#include "test_raytracer_plugin.moc"
