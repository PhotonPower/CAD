#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

#include "viewport/viewport.hpp"

using nfcad::viewport::Viewport;
using nfcad::viewport::ViewKind;

namespace {

void drag(Viewport& v, Qt::MouseButton button, Qt::KeyboardModifiers mods, QPoint from,
          QPoint to) {
  QTest::mousePress(&v, button, mods, from);
  QMouseEvent move(QEvent::MouseMove, QPointF(to), v.mapToGlobal(QPointF(to)), Qt::NoButton,
                   button, mods);
  QApplication::sendEvent(&v, &move);
  QTest::mouseRelease(&v, button, mods, to);
}

void wheel(Viewport& v, QPoint pos, int angle_y) {
  QWheelEvent e(QPointF(pos), v.mapToGlobal(QPointF(pos)), QPoint(), QPoint(0, angle_y),
                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&v, &e);
}

}  // namespace

class TestViewportInput : public QObject {
  Q_OBJECT
 private slots:
  void wheelZoomsTowardsCursor() {
    Viewport v(ViewKind::Top);
    v.resize(800, 600);
    const float h0 = v.camera().halfHeight();
    wheel(v, {400, 300}, 120);
    QVERIFY(v.camera().halfHeight() < h0);
    wheel(v, {400, 300}, -240);
    QVERIFY(v.camera().halfHeight() > h0);
  }

  void middleDragPans() {
    Viewport v(ViewKind::Front);
    v.resize(800, 600);
    const QVector3D t0 = v.camera().target();
    drag(v, Qt::MiddleButton, Qt::NoModifier, {400, 300}, {450, 300});
    const QVector3D d = v.camera().target() - t0;
    QVERIFY(d.x() < 0.0F);  // scene follows the cursor to the right -> target moves left
    QCOMPARE(v.camera().forward(), QVector3D(0, 1, 0));
  }

  void rightDragOrbitsOnlyInPerspective() {
    Viewport front(ViewKind::Front);
    front.resize(800, 600);
    drag(front, Qt::RightButton, Qt::NoModifier, {400, 300}, {500, 350});
    QCOMPARE(front.camera().forward(), QVector3D(0, 1, 0));  // panned, not rotated

    Viewport persp(ViewKind::Perspective);
    persp.resize(800, 600);
    const QVector3D f0 = persp.camera().forward();
    drag(persp, Qt::LeftButton, Qt::AltModifier, {400, 300}, {500, 300});
    QVERIFY((persp.camera().forward() - f0).length() > 0.1F);
  }

  void cursorReportsPlanePoint() {
    Viewport v(ViewKind::Top);
    v.resize(800, 600);
    QSignalSpy spy(&v, &Viewport::cursorMoved);
    QMouseEvent move(QEvent::MouseMove, QPointF(400, 300), v.mapToGlobal(QPointF(400, 300)),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&v, &move);
    QVERIFY(!spy.isEmpty());
    const auto p = spy.last().at(0).value<QVector3D>();
    QVERIFY(spy.last().at(1).toBool());
    QVERIFY(std::abs(p.z()) < 1e-4F);  // in the XY construction plane
    QVERIFY(p.length() < 1e-3F);       // view centre = origin after setKind
  }

  void zoomExtentsResets() {
    Viewport v(ViewKind::Top);
    v.resize(800, 600);
    wheel(v, {100, 100}, 600);
    v.zoomExtents();
    QVERIFY(v.camera().target().length() < 1e-5F);
  }
};

QTEST_MAIN(TestViewportInput)
#include "test_viewport_input.moc"
