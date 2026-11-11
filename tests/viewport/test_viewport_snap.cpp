#include <QKeyEvent>
#include <QSignalSpy>
#include <QTest>
#include <nurbforge/nurbforge.hpp>

#include "document/document.hpp"
#include "render/scene.hpp"
#include "snap/snap_engine.hpp"
#include "tessellation/tessellation_service.hpp"
#include "viewport/snap_marker.hpp"
#include "viewport/viewport.hpp"

using nfcad::snap::SnapPoint;
using nfcad::snap::SnapType;
using nfcad::viewport::Viewport;
using nfcad::viewport::ViewKind;

Q_DECLARE_METATYPE(nfcad::snap::SnapPoint)
Q_DECLARE_METATYPE(nfcad::snap::SnapType)

namespace {
void mouse(Viewport& v, QEvent::Type type, Qt::MouseButton button, QPointF pos,
           Qt::KeyboardModifiers mods = Qt::NoModifier) {
  const Qt::MouseButtons buttons =
      type == QEvent::MouseButtonRelease || type == QEvent::MouseMove ? Qt::NoButton
                                                                      : Qt::MouseButtons(button);
  QMouseEvent e(type, pos, v.mapToGlobal(pos), type == QEvent::MouseMove ? Qt::NoButton : button,
                buttons, mods);
  QApplication::sendEvent(&v, &e);
}
}  // namespace

class TestViewportSnap : public QObject {
  Q_OBJECT
  nfcad::document::Document doc_;
  nfcad::tessellation::TessellationService service_;
  nfcad::render::Scene scene_{&doc_, &service_};
  nfcad::snap::SnapEngine engine_;

 private slots:
  void initTestCase() {
    qRegisterMetaType<SnapPoint>();
    qRegisterMetaType<SnapType>();
    QSignalSpy idle(&service_, &nfcad::tessellation::TessellationService::idle);
    doc_.addEntities({nfcad::document::make_entity(nf::make_circle({3, 4, 0}, {0, 0, 1}, 5.0)),
                      nfcad::document::make_entity(nf::make_line({-10, -10, 0}, {10, -10, 0}))},
                     QStringLiteral("add"));
    QVERIFY(idle.wait(10000));
  }

  void glyphSheet() {
    // All snap glyphs side by side; with NFCAD_SNAPSHOT_DIR set the sheet is saved for review.
    QWidget sheet;
    sheet.resize(10 * 90, 60);
    sheet.setStyleSheet(QStringLiteral("background: #30343b;"));
    for (std::size_t i = 0; i < nfcad::snap::kSnapPriority.size(); ++i) {
      auto* host = new QWidget(&sheet);
      host->setGeometry(static_cast<int>(i) * 90, 0, 90, 60);
      auto* m = new nfcad::viewport::SnapMarker(host);
      const SnapType t = nfcad::snap::kSnapPriority[i];
      m->showAt(QPointF(16, 34), t, QString::fromLatin1(nfcad::snap::short_name(t)));
      QVERIFY(m->isVisibleTo(host));
    }
    const QImage img = sheet.grab().toImage();
    QVERIFY(!img.isNull());
    if (const QByteArray dir = qgetenv("NFCAD_SNAPSHOT_DIR"); !dir.isEmpty()) {
      img.save(QString::fromLocal8Bit(dir) + QStringLiteral("/snap_glyphs.png"));
    }
  }

  void snapClickTabAlt() {
    Viewport v(ViewKind::Top);
    v.resize(800, 600);
    v.setScene(&scene_);
    v.setSnapEngine(&engine_);
    v.zoomExtents();
    v.setPointInput(true);
    QSignalSpy picked(&v, &Viewport::pointPicked);
    QSignalSpy finished(&v, &Viewport::pointInputFinished);

    // magnet: a few pixels off the circle centre
    const QPointF centre = *v.camera().project({3, 4, 0}) + QPointF(4, -3);
    mouse(v, QEvent::MouseMove, Qt::NoButton, centre);
    QVERIFY(!v.snapCandidates().empty());
    QCOMPARE(v.snapCandidates().front().type, SnapType::Centre);
    mouse(v, QEvent::MouseButtonPress, Qt::LeftButton, centre);
    mouse(v, QEvent::MouseButtonRelease, Qt::LeftButton, centre);
    QCOMPARE(picked.count(), 1);
    auto p = picked.last().at(0).value<SnapPoint>();
    QVERIFY(picked.last().at(1).toBool());
    QCOMPARE(picked.last().at(2).value<SnapType>(), SnapType::Centre);
    QCOMPARE(p.x, 3.0);
    QCOMPARE(p.y, 4.0);

    // line end (-10,-10) is also near the circle? no: Tab cycles End -> Near at the end point
    const QPointF end = *v.camera().project({10, -10, 0}) + QPointF(2, 2);
    mouse(v, QEvent::MouseMove, Qt::NoButton, end);
    QVERIFY(v.snapCandidates().size() >= 2);
    QCOMPARE(v.snapCandidates().front().type, SnapType::End);
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(&v, &tab);
    mouse(v, QEvent::MouseButtonPress, Qt::LeftButton, end);
    mouse(v, QEvent::MouseButtonRelease, Qt::LeftButton, end);
    QCOMPARE(picked.count(), 2);
    QVERIFY(picked.last().at(2).value<SnapType>() != SnapType::End);  // the second candidate

    // Alt suppresses snapping: the point lies in the construction plane under the cursor
    mouse(v, QEvent::MouseMove, Qt::NoButton, centre, Qt::AltModifier);
    QVERIFY(v.snapCandidates().empty());
    mouse(v, QEvent::MouseButtonPress, Qt::LeftButton, centre, Qt::AltModifier);
    mouse(v, QEvent::MouseButtonRelease, Qt::LeftButton, centre, Qt::AltModifier);
    QCOMPARE(picked.count(), 3);
    QVERIFY(!picked.last().at(1).toBool());
    p = picked.last().at(0).value<SnapPoint>();
    QVERIFY(std::abs(p.x - 3.0) > 1e-3);  // not snapped to the centre
    QCOMPARE(p.z, 0.0);

    // right click finishes
    mouse(v, QEvent::MouseButtonPress, Qt::RightButton, centre);
    mouse(v, QEvent::MouseButtonRelease, Qt::RightButton, centre);
    QCOMPARE(finished.count(), 1);

    // snapping off (F3)
    v.setSnapSettings(nfcad::snap::SnapMask::defaults(), false);
    mouse(v, QEvent::MouseMove, Qt::NoButton, centre + QPointF(1, 0));
    QVERIFY(v.snapCandidates().empty());
  }
};

QTEST_MAIN(TestViewportSnap)
#include "test_viewport_snap.moc"
