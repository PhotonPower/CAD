#include <QSignalSpy>
#include <QTest>
#include <nurbforge/nurbforge.hpp>

#include "document/document.hpp"
#include "document/selection.hpp"
#include "render/scene.hpp"
#include "tessellation/tessellation_service.hpp"
#include "viewport/viewport.hpp"

using nfcad::document::Selection;
using nfcad::viewport::Viewport;
using nfcad::viewport::ViewKind;

namespace {

void send(Viewport& v, QEvent::Type type, Qt::MouseButton button, QPointF pos,
          Qt::KeyboardModifiers mods = Qt::NoModifier) {
  const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(button);
  QMouseEvent e(type, pos, v.mapToGlobal(pos), type == QEvent::MouseMove ? Qt::NoButton : button,
                buttons, mods);
  QApplication::sendEvent(&v, &e);
}

}  // namespace

class TestViewportSelection : public QObject {
  Q_OBJECT
  nfcad::document::Document doc_;
  nfcad::tessellation::TessellationService service_;
  nfcad::render::Scene scene_{&doc_, &service_};
  Selection selection_;
  std::vector<nfcad::document::EntityId> ids_;

 private slots:
  void initTestCase() {
    selection_.attach(&doc_);
    QSignalSpy idle(&service_, &nfcad::tessellation::TessellationService::idle);
    ids_ = doc_.addEntities({nfcad::document::make_entity(nf::make_box({-10, -10, 0}, {8, 8, 4})),
                             nfcad::document::make_entity(nf::make_box({2, 2, 0}, {8, 8, 4}))},
                            QStringLiteral("add"));
    QVERIFY(idle.wait(10000));
    QVERIFY(scene_.bounds().valid());
  }

  void clickHoverWindow() {
    Viewport v(ViewKind::Top);
    v.resize(800, 600);
    v.setScene(&scene_);
    v.setSelection(&selection_);
    v.zoomExtents();
    const QPointF a = *v.camera().project({-6, -6, 4});
    const QPointF b = *v.camera().project({6, 6, 4});

    // hover
    send(v, QEvent::MouseMove, Qt::NoButton, a);
    QVERIFY(selection_.hovered());
    QCOMPARE(selection_.hovered()->entity, ids_[0]);

    // click, shift-click, cmd/ctrl-click, click into the void
    send(v, QEvent::MouseButtonPress, Qt::LeftButton, a);
    send(v, QEvent::MouseButtonRelease, Qt::LeftButton, a);
    QCOMPARE(selection_.entities(), std::vector{ids_[0]});
    send(v, QEvent::MouseButtonPress, Qt::LeftButton, b, Qt::ShiftModifier);
    send(v, QEvent::MouseButtonRelease, Qt::LeftButton, b, Qt::ShiftModifier);
    QCOMPARE(selection_.items().size(), std::size_t{2});
    send(v, QEvent::MouseButtonPress, Qt::LeftButton, a, Qt::ControlModifier);
    send(v, QEvent::MouseButtonRelease, Qt::LeftButton, a, Qt::ControlModifier);
    QCOMPARE(selection_.entities(), std::vector{ids_[1]});
    const QPointF empty = *v.camera().project({-8, 8, 0});
    send(v, QEvent::MouseButtonPress, Qt::LeftButton, empty);
    send(v, QEvent::MouseButtonRelease, Qt::LeftButton, empty);
    QVERIFY(selection_.empty());

    // window (left to right) around the first box only
    const QPointF w0 = *v.camera().project({-11, 0, 0});
    const QPointF w1 = *v.camera().project({-1, -11, 0});
    send(v, QEvent::MouseButtonPress, Qt::LeftButton, w0);
    send(v, QEvent::MouseMove, Qt::LeftButton, w1);
    send(v, QEvent::MouseButtonRelease, Qt::LeftButton, w1);
    QCOMPARE(selection_.entities(), std::vector{ids_[0]});

    // crossing (right to left) touching both boxes
    const QPointF c0 = *v.camera().project({4, 4, 0});
    const QPointF c1 = *v.camera().project({-4, -4, 0});
    send(v, QEvent::MouseButtonPress, Qt::LeftButton, c0);
    send(v, QEvent::MouseMove, Qt::LeftButton, c1);
    send(v, QEvent::MouseButtonRelease, Qt::LeftButton, c1);
    QCOMPARE(selection_.items().size(), std::size_t{2});
  }

  void subElementClick() {
    Viewport v(ViewKind::Top);
    v.resize(800, 600);
    v.setScene(&scene_);
    v.setSelection(&selection_);
    v.zoomExtents();
    selection_.clear();
    const QPointF a = *v.camera().project({-6, -6, 4});
    const auto mods = Qt::ControlModifier | Qt::ShiftModifier;
    send(v, QEvent::MouseButtonPress, Qt::LeftButton, a, mods);
    send(v, QEvent::MouseButtonRelease, Qt::LeftButton, a, mods);
    QCOMPARE(selection_.items().size(), std::size_t{1});
    QCOMPARE(selection_.items().begin()->sub.kind, nfcad::document::SubElement::Kind::Face);
    // same click again toggles it off
    send(v, QEvent::MouseButtonPress, Qt::LeftButton, a, mods);
    send(v, QEvent::MouseButtonRelease, Qt::LeftButton, a, mods);
    QVERIFY(selection_.empty());
  }

  void zoomSelected() {
    using nfcad::document::SelectionItem;
    using nfcad::document::SubElement;
    Viewport v(ViewKind::Top);
    v.resize(800, 600);
    v.setScene(&scene_);
    v.setSelection(&selection_);
    // without a selection: zoom extents
    selection_.clear();
    v.zoomExtents();
    const QVector3D extents_target = v.camera().target();
    const float extents_distance = v.camera().distance();
    v.zoomSelected();
    QVERIFY((v.camera().target() - extents_target).length() < 1e-4F);
    QCOMPARE(v.camera().distance(), extents_distance);
    // the second box: centred and filling the view, closer than zoom extents
    selection_.apply({SelectionItem{ids_[1], {}}}, Selection::Mode::Replace);
    const nfcad::tessellation::Box3 box = scene_.bounds(selection_.ordered());
    QVERIFY(box.valid());
    v.zoomSelected();
    QVERIFY((v.camera().target() - box.center()).length() < 1e-3F * box.radius() + 1e-4F);
    QVERIFY(v.camera().distance() < extents_distance);
    const QPointF lo = *v.camera().project(box.min);
    const QPointF hi = *v.camera().project(box.max);
    const double w = std::abs(hi.x() - lo.x());
    const double h = std::abs(hi.y() - lo.y());
    QVERIFY(w <= 800.5 && h <= 600.5);   // inside the view
    QVERIFY(w > 0.5 * 800 || h > 0.5 * 600);  // and filling it
    // a sub-element: its bounds lie inside those of its entity and are smaller
    const nfcad::tessellation::Box3 whole = scene_.bounds({SelectionItem{ids_[0], {}}});
    const nfcad::tessellation::Box3 face = scene_.bounds({SelectionItem{ids_[0], SubElement{SubElement::Kind::Face, 0}}});
    QVERIFY(face.valid());
    QVERIFY(face.radius() < whole.radius());
    QVERIFY(face.min.x() >= whole.min.x() - 1e-4F && face.max.z() <= whole.max.z() + 1e-4F);
    selection_.clear();
  }
};

QTEST_MAIN(TestViewportSelection)
#include "test_viewport_selection.moc"
