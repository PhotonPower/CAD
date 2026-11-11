#include <QElapsedTimer>
#include <QTest>
#include <cmath>
#include <numbers>
#include <nurbforge/nurbforge.hpp>

#include "kernel/curve_features.hpp"
#include "snap/snap_engine.hpp"
#include "tessellation/tessellator.hpp"
#include "viewport/view_kind.hpp"

using nfcad::document::EntityId;
using nfcad::snap::SnapEngine;
using nfcad::snap::SnapMask;
using nfcad::snap::SnapQuery;
using nfcad::snap::SnapResult;
using nfcad::snap::SnapType;
using nfcad::tessellation::DisplayMesh;
using nfcad::viewport::Camera;
using nfcad::viewport::ViewKind;

namespace {

struct Item {
  EntityId id;
  nfcad::document::GeometryPtr geometry;
  std::shared_ptr<DisplayMesh> mesh;
};

Item item(EntityId id, nfcad::document::Geometry g) {
  auto geometry = std::make_shared<const nfcad::document::Geometry>(std::move(g));
  auto mesh = std::make_shared<DisplayMesh>(
      nfcad::tessellation::tessellate(*geometry, nfcad::tessellation::default_params(*geometry)));
  return {id, geometry, mesh};
}

std::vector<nfcad::pick::Target> targets(const std::vector<Item>& items) {
  std::vector<nfcad::pick::Target> out;
  for (const Item& i : items) out.push_back({i.id, i.mesh.get(), i.geometry});
  return out;
}

Camera camera(ViewKind kind) {
  const auto preset = nfcad::viewport::view_preset(kind);
  Camera c;
  c.setViewportSize({800, 600});
  c.setView(preset.forward, preset.up);
  c.setProjection(preset.projection);
  c.setTarget({0, 0, 0});
  c.setHalfHeight(30.0F);  // 0.1 units per pixel
  c.setSceneRadius(100.0F);
  return c;
}

double dist(const SnapResult& r, const nf::Vec3& p) {
  return std::hypot(r.point.x - p.x, r.point.y - p.y, r.point.z - p.z);
}

}  // namespace

class TestSnapEngine : public QObject {
  Q_OBJECT
  // 1: line (-20,-20,0)-(0,0,0)      2: circle centre (10,10,0) r 5 in XY
  // 3: line ( 0,-20,0)-(20,0,0)      crosses line 1? no; crosses circle? see tests
  // 4: box shell (-25, 5, 0) size 8 x 8 x 4      5: point (15,-15,0)
  // 6: line (5,-10,0)-(5,30,0)  crosses the circle (exact intersections at x = 5)
  // 7: line (-18,-4,3)-(-4,-16,3), 3 units above line 1: crosses it only in the top view
  std::vector<Item> items_ = {
      item(1, nf::make_line({-20, -20, 0}, {0, 0, 0})),
      item(2, nf::make_circle({10, 10, 0}, {0, 0, 1}, 5.0)),
      item(4, nf::make_box({-25, 5, 0}, {8, 8, 4})),
      item(5, nf::Vec3{15, -15, 0}),
      item(6, nf::make_line({5, -10, 0}, {5, 30, 0})),
      item(7, nf::make_line({-18, -4, 3}, {-4, -16, 3})),
  };
  SnapEngine engine_;

  std::vector<SnapResult> at(const nf::Vec3& p, SnapMask mask = SnapMask::defaults(),
                             ViewKind kind = ViewKind::Top, QPointF offset = {2, -1}) {
    const Camera c = camera(kind);
    SnapQuery q;
    q.cursor_px = *c.project({static_cast<float>(p.x), static_cast<float>(p.y),
                              static_cast<float>(p.z)}) + offset;
    q.mask = mask;
    q.faces_occlude = true;
    return engine_.query(targets(items_), c, q);
  }

  static SnapMask only(SnapType t) {
    SnapMask m;
    m.set(t, true);
    return m;
  }

 private slots:
  // ---- curve features (kernel)
  void circleFeatures() {
    const auto f = nfcad::kernel::curve_features(nf::make_circle({1, 2, 3}, {0, 0, 1}, 4.0));
    QVERIFY(f.closed);
    QVERIFY(!f.mid && !f.start);
    QVERIFY(f.centre && nf::distance(*f.centre, {1, 2, 3}) < 1e-9);
    QCOMPARE(f.quadrants.size(), std::size_t{4});  // ±X, ±Y; the Z-plane contains the circle
    for (const auto& p : f.quadrants) QVERIFY(std::abs(nf::distance(p, {1, 2, 3}) - 4.0) < 1e-9);
  }

  void tiltedCircleHasSixAxisPoints() {
    const nf::Vec3 n = nf::Vec3{1, 1, 1} / std::sqrt(3.0);
    const auto f = nfcad::kernel::curve_features(nf::make_circle({0, 0, 0}, n, 2.0));
    QCOMPARE(f.quadrants.size(), std::size_t{6});
    // each point lies on a coordinate plane through the centre
    for (const auto& p : f.quadrants) {
      QVERIFY(std::min({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) < 1e-9);
    }
  }

  void arcKeepsOnlyItsQuadrants() {
    // quarter arc from +X to +Y
    const auto f = nfcad::kernel::curve_features(
        nf::make_arc({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 3.0, 0.0, std::numbers::pi / 2));
    QVERIFY(!f.closed);
    QCOMPARE(f.quadrants.size(), std::size_t{2});  // its end points (0° and 90°)
    QVERIFY(f.mid);
    const double c = 3.0 / std::sqrt(2.0);
    QVERIFY(nf::distance(*f.mid, {c, c, 0}) < 1e-7);  // half the arc length
  }

  void closedPolylineCentroid() {
    const auto f = nfcad::kernel::curve_features(
        nf::make_polyline({{0, 0, 0}, {4, 0, 0}, {4, 2, 0}, {0, 2, 0}, {0, 0, 0}}));
    QVERIFY(f.closed);
    QVERIFY(f.centre);
    QVERIFY(nf::distance(*f.centre, {2, 1, 0}) < 1e-9);
  }

  // ---- snap types (engine, exact results)
  void endSnap() {
    const auto r = at({-20, -20, 0});
    QVERIFY(!r.empty());
    QCOMPARE(r.front().type, SnapType::End);
    QVERIFY(dist(r.front(), {-20, -20, 0}) < 1e-12);
  }

  void midSnap() {
    const auto r = at({-10, -10, 0}, only(SnapType::Mid));
    QVERIFY(!r.empty());
    QVERIFY(dist(r.front(), {-10, -10, 0}) < 1e-9);
  }

  void centreSnap() {
    const auto r = at({10, 10, 0});
    QVERIFY(!r.empty());
    QCOMPARE(r.front().type, SnapType::Centre);
    QVERIFY(dist(r.front(), {10, 10, 0}) < 1e-9);
  }

  void quadrantSnap() {
    const auto r = at({15, 10, 0});
    QVERIFY(!r.empty());
    QCOMPARE(r.front().type, SnapType::Quadrant);
    QVERIFY(dist(r.front(), {15, 10, 0}) < 1e-9);
  }

  void nearSnapIsExactlyOnTheCircle() {
    const double a = 0.3;
    const nf::Vec3 on(10 + 5 * std::cos(a), 10 + 5 * std::sin(a), 0);
    const auto r = at(on, only(SnapType::Near));
    QVERIFY(!r.empty());
    QVERIFY(std::abs(std::hypot(r.front().point.x - 10, r.front().point.y - 10) - 5.0) < 1e-9);
    QCOMPARE(r.front().point.z, 0.0);
  }

  void intersectionSnapExact() {
    // line x = 5 cuts the circle at (5, 10 ± 0)?  (x-10)^2 + (y-10)^2 = 25 -> y = 10
    const auto r = at({5, 10, 0}, only(SnapType::Intersection));
    QVERIFY(!r.empty());
    QVERIFY(!r.front().apparent);
    QVERIFY2(dist(r.front(), {5, 10, 0}) < 1e-7, qPrintable(QString::number(dist(r.front(), {5, 10, 0}))));
    QCOMPARE(r.front().entity + r.front().other, EntityId{2 + 6});
  }

  void apparentIntersectionInTopView() {
    // line 7 lies 3 units above line 1; in the top view they cross where x = y (line 1: y = x)
    // line 7: y = -4 - (x + 18) * 6 / 7  ->  x = y = -136 / 13
    const double x = -136.0 / 13.0;
    const auto r = at({x, x, 0}, only(SnapType::Intersection), ViewKind::Top, {1, 1});
    QVERIFY(!r.empty());
    QVERIFY(r.front().apparent);
    QVERIFY(std::abs(r.front().point.z - 3.0) < 1e-9);  // the upper line is in front
    QVERIFY(std::abs(r.front().point.x - x) < 1e-3 && std::abs(r.front().point.y - x) < 1e-3);
    // in the perspective view no apparent intersections are offered
    for (const SnapResult& p : at({x, x, 0}, only(SnapType::Intersection), ViewKind::Perspective, {0, 0})) {
      QVERIFY(!p.apparent);
    }
  }

  void faceCornerAndEdgeMid() {
    auto r = at({-17, 13, 4});  // top corner of the box
    QVERIFY(!r.empty());
    QCOMPARE(r.front().type, SnapType::FaceCorner);
    QVERIFY(dist(r.front(), {-17, 13, 4}) < 1e-9);
    r = at({-21, 13, 4}, only(SnapType::Mid) | SnapType::FaceEdge);
    QVERIFY(!r.empty());
    QCOMPARE(r.front().type, SnapType::Mid);
    QVERIFY(dist(r.front(), {-21, 13, 4}) < 1e-9);
    // without FaceEdge the edge mid is not offered
    QVERIFY(at({-21, 13, 4}, only(SnapType::Mid)).empty());
  }

  void pointSnapAndPriority() {
    auto r = at({15, -15, 0});
    QVERIFY(!r.empty());
    QCOMPARE(r.front().type, SnapType::Point);
    // at the end of line 1 both End and Near apply: End wins
    r = at({0, 0, 0}, SnapMask::defaults(), ViewKind::Top, {1, 0});
    QVERIFY(r.size() >= 2);
    QCOMPARE(r.front().type, SnapType::End);
  }

  void gridSnap() {
    const Camera c = camera(ViewKind::Top);
    SnapQuery q;
    q.cursor_px = *c.project({31.3F, -27.8F, 0}) ;
    q.mask = only(SnapType::Grid);
    q.grid = std::make_pair(nfcad::viewport::CPlane{}, 1.0F);
    const auto r = engine_.query(targets(items_), c, q);
    QVERIFY(!r.empty());
    QVERIFY(dist(r.front(), {31, -28, 0}) < 1e-6);
  }

  void hiddenByFaces() {
    // a line under the box top face, seen from the top: hidden in surfaces / shaded mode
    std::vector<Item> items = {item(1, nf::make_box({0, 0, 0}, {10, 10, 4})),
                               item(2, nf::make_line({2, 5, 1}, {8, 5, 1}))};
    const Camera c = camera(ViewKind::Top);
    SnapQuery q;
    q.cursor_px = *c.project({8, 5, 1});
    q.mask = only(SnapType::End);
    SnapEngine e;
    QVERIFY(e.query(targets(items), c, q).empty());
    q.faces_occlude = false;  // wireframe
    QCOMPARE(e.query(targets(items), c, q).size(), std::size_t{1});
  }

  void maskAndRadius() {
    QVERIFY(at({-20, -20, 0}, SnapMask{}).empty());
    QVERIFY(at({-20, -20, 0}, SnapMask::defaults(), ViewKind::Top, {30, 0}).empty());  // 30 px away
  }

  void performanceTenThousandCurves() {
    // docs/plan.md, M4: < 2 ms per query with 10 000 curves (release build)
    std::vector<Item> items;
    for (int i = 0; i < 100; ++i) {
      for (int j = 0; j < 100; ++j) {
        const double x = 3.0 * i - 150.0;
        const double y = 3.0 * j - 150.0;
        items.push_back((i + j) % 2 == 0
                            ? item(static_cast<EntityId>(items.size() + 1), nf::make_line({x, y, 0}, {x + 2.5, y + 1, 0}))
                            : item(static_cast<EntityId>(items.size() + 1), nf::make_circle({x, y, 0}, {0, 0, 1}, 1.2)));
      }
    }
    const auto t = targets(items);
    Camera c = camera(ViewKind::Top);
    c.setHalfHeight(160.0F);
    SnapEngine e;
    SnapQuery q;
    // warm-up pass: per-entity caches are built lazily on first contact
    for (int k = 0; k < 400; ++k) {
      q.cursor_px = QPointF(50 + (k * 37) % 700, 40 + (k * 53) % 520);
      (void)e.query(t, c, q);
    }
    QElapsedTimer timer;
    timer.start();
    constexpr int kQueries = 400;
    std::size_t found = 0;
    for (int k = 0; k < kQueries; ++k) {
      q.cursor_px = QPointF(50 + (k * 37) % 700, 40 + (k * 53) % 520);
      found += e.query(t, c, q).size();
    }
    const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6 / kQueries;
    qInfo("10000 curves: %.3f ms per snap query (%zu candidates)", ms, found);
    QVERIFY(found > 0);
#ifdef NDEBUG
    QVERIFY2(ms < 2.0, qPrintable(QString::number(ms)));
#endif
  }
};

QTEST_GUILESS_MAIN(TestSnapEngine)
#include "test_snap_engine.moc"
