#include <QTest>
#include <cmath>

#include "viewport/grid.hpp"

using nfcad::viewport::build_grid;
using nfcad::viewport::CPlane;
using nfcad::viewport::grid_spacing;

class TestGrid : public QObject {
  Q_OBJECT
 private slots:
  void spacingIsPowerOfTenAboveMinimum() {
    QCOMPARE(grid_spacing(0.1F, 12.0F), 10.0F);   // 1.2 world units per 12 px -> 10
    QCOMPARE(grid_spacing(0.05F, 12.0F), 1.0F);   // 0.6 -> 1
    QVERIFY(std::abs(grid_spacing(0.0001F, 12.0F) - 0.01F) < 1e-6F);
  }

  void gridLiesInPlaneAndHasAxes() {
    CPlane front{{}, {1, 0, 0}, {0, 0, 1}};  // XZ
    const auto v = build_grid(front, {3, 0, 4}, 1.0F, 15.0F);
    QVERIFY(!v.empty());
    QCOMPARE(v.size() % 2, std::size_t{0});
    for (const auto& p : v) QCOMPARE(p.y, 0.0F);
    // last two lines are the axes in world colours: u = X red, v = Z blue
    const auto& u_axis = v[v.size() - 4];
    const auto& v_axis = v[v.size() - 2];
    QVERIFY(u_axis.r > u_axis.g && u_axis.r > u_axis.b);
    QVERIFY(v_axis.b > v_axis.r && v_axis.b > v_axis.g);
    QCOMPARE(u_axis.z, 0.0F);  // u axis: v = 0 -> z = 0
    QCOMPARE(v_axis.x, 0.0F);  // v axis: u = 0 -> x = 0
  }

  void gridAnchoredAtMajorSpacing() {
    CPlane top;
    const auto a = build_grid(top, {0.3F, 0.2F, 0}, 1.0F, 20.0F);
    const auto b = build_grid(top, {2.9F, -3.1F, 0}, 1.0F, 20.0F);
    QCOMPARE(a.size(), b.size());  // same major cell -> same lines
    QCOMPARE(a.front().x, b.front().x);
  }

  void lineCountIsBounded() {
    CPlane top;
    const auto v = build_grid(top, {}, 1e-3F, 1e6F);
    QVERIFY(v.size() <= 2 * 2 * 401 + 4);
  }
};

QTEST_GUILESS_MAIN(TestGrid)
#include "test_grid.moc"
