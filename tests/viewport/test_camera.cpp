#include <QTest>
#include <cmath>

#include "viewport/camera.hpp"
#include "viewport/view_kind.hpp"

using nfcad::viewport::Camera;
using nfcad::viewport::ViewKind;

namespace {

bool near(const QPointF& a, const QPointF& b, double tol = 1e-2) {
  return std::abs(a.x() - b.x()) < tol && std::abs(a.y() - b.y()) < tol;
}
bool near(const QVector3D& a, const QVector3D& b, float tol = 1e-4F) {
  return (a - b).length() < tol;
}

Camera preset_camera(ViewKind kind) {
  const auto p = nfcad::viewport::view_preset(kind);
  Camera c;
  c.setViewportSize({800, 600});
  c.setView(p.forward, p.up);
  c.setProjection(p.projection);
  c.setTarget({1.0F, 2.0F, 3.0F});
  c.setHalfHeight(25.0F);
  return c;
}

}  // namespace

class TestCamera : public QObject {
  Q_OBJECT
 private slots:
  void standardViewAxes_data() {
    QTest::addColumn<int>("kind");
    QTest::addColumn<QVector3D>("right");
    QTest::addColumn<QVector3D>("up");
    QTest::newRow("front") << int(ViewKind::Front) << QVector3D(1, 0, 0) << QVector3D(0, 0, 1);
    QTest::newRow("left") << int(ViewKind::Left) << QVector3D(0, -1, 0) << QVector3D(0, 0, 1);
    QTest::newRow("top") << int(ViewKind::Top) << QVector3D(1, 0, 0) << QVector3D(0, 1, 0);
  }
  void standardViewAxes() {
    QFETCH(int, kind);
    QFETCH(QVector3D, right);
    QFETCH(QVector3D, up);
    const Camera c = preset_camera(static_cast<ViewKind>(kind));
    QVERIFY(near(c.right(), right));
    QVERIFY(near(c.up(), up));
    // A point to the right of the target appears right of the centre, one above appears above.
    const auto pr = c.project(c.target() + right);
    const auto pu = c.project(c.target() + up);
    QVERIFY(pr && pr->x() > 400.0);
    QVERIFY(pu && pu->y() < 300.0);
  }

  void targetProjectsToCentre_data() { projections(); }
  void targetProjectsToCentre() {
    QFETCH(int, projection);
    Camera c = preset_camera(ViewKind::Perspective);
    c.setProjection(static_cast<Camera::Projection>(projection));
    const auto p = c.project(c.target());
    QVERIFY(p);
    QVERIFY(near(*p, QPointF(400, 300)));
  }

  void targetPlaneRoundTrip_data() { projections(); }
  void targetPlaneRoundTrip() {
    QFETCH(int, projection);
    Camera c = preset_camera(ViewKind::Perspective);
    c.setProjection(static_cast<Camera::Projection>(projection));
    for (const QPointF px : {QPointF(0, 0), QPointF(123.5, 456.25), QPointF(799, 599)}) {
      const auto back = c.project(c.pointOnTargetPlane(px));
      QVERIFY(back);
      QVERIFY2(near(*back, px), qPrintable(QStringLiteral("%1,%2").arg(back->x()).arg(back->y())));
    }
  }

  void pickRayHitsProjectedPoint_data() { projections(); }
  void pickRayHitsProjectedPoint() {
    QFETCH(int, projection);
    Camera c = preset_camera(ViewKind::Perspective);
    c.setProjection(static_cast<Camera::Projection>(projection));
    const QVector3D p(4.0F, -3.0F, 7.0F);
    const auto px = c.project(p);
    QVERIFY(px);
    const auto ray = c.pickRay(*px);
    // distance from p to the ray
    const QVector3D w = p - ray.origin;
    const QVector3D perp = w - ray.direction * QVector3D::dotProduct(w, ray.direction);
    QVERIFY2(perp.length() < 1e-3F, qPrintable(QString::number(perp.length())));
  }

  void zoomKeepsPointUnderCursor_data() { projections(); }
  void zoomKeepsPointUnderCursor() {
    QFETCH(int, projection);
    Camera c = preset_camera(ViewKind::Perspective);
    c.setProjection(static_cast<Camera::Projection>(projection));
    const QPointF cursor(610, 140);
    const QVector3D under = c.pointOnTargetPlane(cursor);
    c.zoomAt(cursor, 0.5F);
    QCOMPARE(c.halfHeight(), 12.5F);
    const auto after = c.project(under);
    QVERIFY(after);
    QVERIFY(near(*after, cursor));
  }

  void panFollowsCursor_data() { projections(); }
  void panFollowsCursor() {
    QFETCH(int, projection);
    Camera c = preset_camera(ViewKind::Front);
    c.setProjection(static_cast<Camera::Projection>(projection));
    const QVector3D p = c.pointOnTargetPlane({300, 200});
    c.pan({40, -25});
    const auto after = c.project(p);
    QVERIFY(after);
    QVERIFY(near(*after, QPointF(340, 175)));
  }

  void orbitKeepsTargetAndStopsAtPole() {
    Camera c = preset_camera(ViewKind::Perspective);
    const QVector3D target = c.target();
    c.orbit({120, 0});
    QVERIFY(near(c.target(), target));
    QVERIFY(std::abs(QVector3D::dotProduct(c.forward(), c.up())) < 1e-5F);
    c.orbit({0, 5000});  // far beyond the pole
    QVERIFY(c.forward().z() > -1.0F);
    QVERIFY(c.up().z() > 0.0F);  // never upside down
    c.orbit({0, -10000});
    QVERIFY(c.up().z() > 0.0F);
  }

  void fitSphereShowsSphere() {
    Camera c = preset_camera(ViewKind::Top);
    c.setViewportSize({400, 800});  // portrait: width limits
    c.fitSphere({5, 5, 0}, 10.0F);
    for (const QVector3D d : {QVector3D(10, 0, 0), QVector3D(-10, 0, 0), QVector3D(0, 10, 0)}) {
      const auto p = c.project(QVector3D(5, 5, 0) + d);
      QVERIFY(p);
      QVERIFY(p->x() >= 0 && p->x() <= 400 && p->y() >= 0 && p->y() <= 800);
    }
  }

  void fitBoxIsTightInOrtho() {
    Camera c = preset_camera(ViewKind::Front);  // ortho, 800x600
    c.fitBox({0, 0, 0}, {100, 10, 5});          // wide and flat
    const auto left = c.project({0, 5, 2.5F});
    const auto right = c.project({100, 5, 2.5F});
    QVERIFY(left && right);
    const double width = right->x() - left->x();
    QVERIFY2(width > 0.85 * 800 && width <= 800, qPrintable(QString::number(width)));
  }

 private:
  static void projections() {
    QTest::addColumn<int>("projection");
    QTest::newRow("ortho") << int(Camera::Projection::Orthographic);
    QTest::newRow("perspective") << int(Camera::Projection::Perspective);
  }
};

QTEST_GUILESS_MAIN(TestCamera)
#include "test_camera.moc"
