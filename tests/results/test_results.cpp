#include <QSignalSpy>
#include <QTest>
#include <cmath>
#include <nurbforge/nurbforge.hpp>

#include "app/demo_results.hpp"
#include "document/document.hpp"
#include "render/scene.hpp"
#include "results/colormap.hpp"
#include "results/result_store.hpp"
#include "tessellation/tessellation_service.hpp"
#include "viewport/viewport.hpp"

using namespace nfcad;

class TestResults : public QObject {
  Q_OBJECT
 private slots:
  void colormaps() {
    using results::Colormap;
    const auto lo = results::colormap_rgba(Colormap::Viridis, 0.0);
    const auto hi = results::colormap_rgba(Colormap::Viridis, 1.0);
    QCOMPARE(int(lo[0]), 68);
    QCOMPARE(int(hi[1]), 231);
    QCOMPARE(results::colormap_rgba(Colormap::Grey, 0.5)[0], static_cast<unsigned char>(128));
    QCOMPARE(results::colormap_rgba(Colormap::Inferno, NAN)[3], static_cast<unsigned char>(0));
    QCOMPARE(results::colormap_rgba(Colormap::Grey, 7.0)[0], static_cast<unsigned char>(255));  // clamped
    QCOMPARE(results::colormap_from_name(QStringLiteral("Inferno")), Colormap::Inferno);
  }

  void fieldTextureAndRange() {
    results::FaceField f;
    f.width = 2;
    f.height = 1;
    f.values = {1.0F, 3.0F};
    f.colormap = results::Colormap::Grey;
    QCOMPARE(f.effectiveRange().min, 1.0);
    QCOMPARE(f.effectiveRange().max, 3.0);
    const auto t = f.texture();
    QCOMPARE(t.size(), std::size_t{8});
    QCOMPARE(int(t[0]), 0);
    QCOMPARE(int(t[4]), 255);
    f.range = results::Range{0.0, 6.0};
    QCOMPARE(int(f.texture()[4]), 128);
  }

  void storeVisibilityAndCurves() {
    results::ResultStore store;
    QSignalSpy changed(&store, &results::ResultStore::changed);
    const auto set = store.createSet(QStringLiteral("run 1"));
    results::LineSet l;
    l.points = {0, 0, 0, 1, 0, 0, 2, 1, 0};
    const auto lines = store.addLines(set, QStringLiteral("rays"), l);
    QCOMPARE(store.item(lines)->lines->polylineCount(), std::size_t{1});  // starts filled in
    const auto curve = store.addCurve(set, QStringLiteral("merit"), {});
    QSignalSpy updated(&store, &results::ResultStore::curveUpdated);
    store.appendToCurve(curve, 1.0, 0.5);
    store.appendToCurve(curve, 2.0, 0.25);
    QCOMPARE(updated.count(), 2);
    QCOMPARE(store.item(curve)->curve->y.size(), std::size_t{2});
    QCOMPARE(store.visibleItems(results::ResultItem::Kind::Lines).size(), std::size_t{1});
    store.setSetVisible(set, false);
    QVERIFY(store.visibleItems(results::ResultItem::Kind::Lines).empty());
    store.removeSet(set);
    QVERIFY(store.sets().empty());
    QVERIFY(changed.count() >= 5);
  }

  void promoteRaysToCurves() {
    results::LineSet l;
    // ray 0: 3 points; ray 1: lost after the second point (NaN); ray 2: 2 points
    l.points = {0, 0, 0, 1, 0, 0, 1, 1, 0,  0, 0, 1, 1, 0, 1, NAN, NAN, NAN,  0, 0, 2, 0, 1, 2};
    l.starts = {0, 3, 6, 8};
    auto all = results::ResultStore::promote(l, 1, 100);
    QCOMPARE(all.size(), std::size_t{3});
    const auto& c1 = std::get<nf::NurbsCurve>(*all[1].geometry);
    QCOMPARE(c1.num_control_points(), std::size_t{2});  // stopped at the lost point
    QCOMPARE(results::ResultStore::promote(l, 2, 100).size(), std::size_t{2});
    QCOMPARE(results::ResultStore::promote(l, 1, 1).size(), std::size_t{1});
  }

  void renderDemoOffscreen() {
    // renders a view with rays and a field into an image (no window); saved for review with
    // NFCAD_SNAPSHOT_DIR. Skipped where no GPU backend is available (CI).
    document::Document doc;
    tessellation::TessellationService service;
    render::Scene scene(&doc, &service);
    results::ResultStore store;
    QSignalSpy idle(&service, &tessellation::TessellationService::idle);
    const auto ids = doc.addEntities(
        {document::make_entity(nf::make_rectangle({-10, -10, 0}, {20, 0, 0}, {0, 20, 0}))},
        QStringLiteral("add"));
    QVERIFY(idle.wait(10000));
    QVERIFY(app::make_demo_results(store, scene, ids[0]).isEmpty());
    QCOMPARE(store.sets().front().items.size(), std::size_t{3});

    viewport::Viewport v(viewport::ViewKind::Perspective);
    v.resize(640, 480);
    v.setScene(&scene);
    v.setResults(&store);
    v.zoomExtents();
    const QImage img = v.grabFramebuffer();
    if (img.isNull()) QSKIP("no RHI backend for offscreen rendering");
    // the field (inferno: dark red / yellow) must be visible somewhere
    int warm = 0;
    for (int y = 0; y < img.height(); y += 4) {
      for (int x = 0; x < img.width(); x += 4) {
        const QColor c = img.pixelColor(x, y);
        warm += (c.red() > 150 && c.blue() < 90) ? 1 : 0;
      }
    }
    QVERIFY2(warm > 20, qPrintable(QString::number(warm)));
    if (const QByteArray dir = qgetenv("NFCAD_SNAPSHOT_DIR"); !dir.isEmpty()) {
      img.save(QString::fromLocal8Bit(dir) + QStringLiteral("/results_demo.png"));
    }
  }
};

QTEST_MAIN(TestResults)
#include "test_results.moc"
