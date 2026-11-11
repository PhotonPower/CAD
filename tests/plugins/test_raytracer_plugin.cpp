// The Raytracer plugin (python/plugins/raytracer, P6) in a real plugin process against a real
// document: load, trace (rays + irradiance), optimise (live geometry), undo. Needs a Python with
// the packages numpy and raytatouille (NFCAD_TEST_PYTHON, else the repository's .venv); skipped
// otherwise – CI has no Raytatouille build.
#include <QDir>
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
