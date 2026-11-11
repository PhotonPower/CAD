#include <QLineEdit>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTest>
#include <nurbforge/nurbforge.hpp>

#include "app/command_controller.hpp"
#include "document/document.hpp"
#include "render/scene.hpp"
#include "tessellation/tessellation_service.hpp"
#include "viewport/viewport.hpp"
#include "viewport/viewport_grid.hpp"

using nfcad::app::CommandController;

namespace {
const nf::NurbsCurve& last_curve(const nfcad::document::Document& doc) {
  return std::get<nf::NurbsCurve>(*doc.entities().rbegin()->second.geometry);
}
void move(nfcad::viewport::Viewport& v, QPointF pos) {
  QMouseEvent e(QEvent::MouseMove, pos, v.mapToGlobal(pos), Qt::NoButton, Qt::NoButton,
                Qt::NoModifier);
  QApplication::sendEvent(&v, &e);
}
}  // namespace

class TestCommandController : public QObject {
  Q_OBJECT
 private slots:
  void typedLineCircleUndoRepeat() {
    nfcad::document::Document doc;
    nfcad::viewport::ViewportGrid grid;
    grid.resize(1000, 800);
    QLineEdit line;
    QStatusBar status;
    CommandController c(&doc, &grid, &line, &status);

    // type a command name, then absolute and relative coordinates
    c.submit(QStringLiteral("line"));
    QVERIFY(c.active());
    QVERIFY(line.placeholderText().contains(QStringLiteral("start")));
    c.submit(QStringLiteral("1,2,3"));
    c.submit(QStringLiteral("@10,0,0"));
    QVERIFY(!c.active());
    QCOMPARE(doc.entities().size(), std::size_t{1});
    const auto& l = last_curve(doc);
    QVERIFY(nf::distance(l.evaluate(l.domain_end()), {11, 2, 3}) < 1e-12);

    // circle by alias, centre and typed radius
    c.submit(QStringLiteral("c"));
    c.submit(QStringLiteral("5,5,0"));
    c.submit(QStringLiteral("2.5"));
    QCOMPARE(doc.entities().size(), std::size_t{2});
    QVERIFY(std::abs(nf::distance(last_curve(doc).evaluate(0.2), {5, 5, 0}) - 2.5) < 1e-12);

    // undo: one step per command
    doc.undoStack()->undo();
    QCOMPARE(doc.entities().size(), std::size_t{1});

    // Enter on an empty line repeats the last command; Esc-like cancel
    c.submit(QString());
    QCOMPARE(c.activeId(), QStringLiteral("circle"));
    c.cancel();
    QVERIFY(!c.active());

    // errors leave the state unchanged
    c.submit(QStringLiteral("nonsense"));
    QVERIFY(!c.active());
    c.submit(QStringLiteral("line"));
    c.submit(QStringLiteral("@1,1"));  // relative without a base point
    QVERIFY(c.active());
    c.cancel();
  }

  void orthoAndTypedLength() {
    nfcad::document::Document doc;
    nfcad::tessellation::TessellationService service;
    nfcad::render::Scene scene(&doc, &service);
    nfcad::viewport::ViewportGrid grid;
    grid.setScene(&scene);
    grid.resize(1000, 800);
    QLineEdit line;
    QStatusBar status;
    CommandController c(&doc, &grid, &line, &status);
    nfcad::viewport::Viewport* top = grid.views()[1];  // Top (XY)
    QCOMPARE(top->kind(), nfcad::viewport::ViewKind::Top);
    top->resize(500, 400);
    top->zoomExtents();

    c.setOrtho(true);
    c.start(QStringLiteral("line"));
    c.submit(QStringLiteral("0,0"));
    // cursor somewhere up-right, mostly along +x: ortho keeps the direction on the x axis
    move(*top, *top->camera().project({6.0F, 1.5F, 0.0F}));
    c.submit(QStringLiteral("4"));  // length 4 along the (ortho) cursor direction
    QVERIFY(!c.active());
    const auto& l = last_curve(doc);
    const auto end = l.evaluate(l.domain_end());
    QVERIFY2(nf::distance(end, {4, 0, 0}) < 1e-6,
             qPrintable(QStringLiteral("%1 %2 %3").arg(end.x).arg(end.y).arg(end.z)));
  }

  void distanceMessageInStatusBar() {
    nfcad::document::Document doc;
    nfcad::viewport::ViewportGrid grid;
    QLineEdit line;
    QStatusBar status;
    CommandController c(&doc, &grid, &line, &status);
    c.start(QStringLiteral("dist"));
    c.submit(QStringLiteral("0,0,0"));
    c.submit(QStringLiteral("0,3,4"));
    QVERIFY(status.currentMessage().startsWith(QStringLiteral("Distance 5")));
    QVERIFY(doc.entities().empty());
  }
};

QTEST_MAIN(TestCommandController)
#include "test_command_controller.moc"
