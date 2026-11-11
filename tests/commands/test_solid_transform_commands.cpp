#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <numbers>
#include <nurbforge/io/step.hpp>
#include <nurbforge/nurbforge.hpp>

#include "commands/command.hpp"
#include "document/document.hpp"
#include "document/selection.hpp"
#include "kernel/geometry_ops.hpp"
#include "tessellation/stl_export.hpp"
#include "tessellation/tessellator.hpp"

using nfcad::commands::Command;
using nfcad::commands::create_command;
using nfcad::commands::PlaneFrame;
using nfcad::document::Document;
using nfcad::document::Entity;
using nfcad::document::EntityId;
using R = Command::Result;

namespace {

const PlaneFrame kTop{};
const PlaneFrame kFront{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}};
constexpr double kPi = std::numbers::pi;

const nf::Shell& shell_of(const Entity& e) { return std::get<nf::Shell>(*e.geometry); }

/// Runs a command on `selection`, applies its edit to `doc`.
void run(Document& doc, std::unique_ptr<Command> c, const std::vector<Entity>& selection,
         const std::function<void(Command&)>& input) {
  QVERIFY(c->begin(selection));
  input(*c);
  doc.apply(c->takeEdit(), c->title());
}

std::vector<Entity> entities(const Document& doc) {
  std::vector<Entity> out;
  for (const auto& [id, e] : doc.entities()) out.push_back(e);
  return out;
}

}  // namespace

class TestSolidTransformCommands : public QObject {
  Q_OBJECT
 private slots:
  void boxFromTypedInput() {
    auto c = create_command(QStringLiteral("box"));
    c->point({0, 0, 0}, kTop);
    c->point({4, 3, 0}, kTop);
    QVERIFY(!c->preview({4, 3, 5}, kTop).empty());
    QCOMPARE(c->number(-2.0), R::Done);  // downwards
    const auto e = c->takeEntities();
    QCOMPARE(e.size(), std::size_t{1});
    QVERIFY(std::abs(shell_of(e[0]).volume() - 24.0) < 1e-9);
  }

  void boxInFrontPlane() {
    auto c = create_command(QStringLiteral("box"));
    c->point({0, 0, 0}, kFront);
    c->point({2, 0, 5}, kFront);  // x 2, z 5
    QCOMPARE(c->point({0, -3, 0}, kFront), R::Done);  // height 3 along the front normal (-y)
    const auto e = c->takeEntities();
    QVERIFY(std::abs(shell_of(e[0]).volume() - 30.0) < 1e-9);
  }

  void sphereCylinderConeTorusVolumes() {
    auto sphere = create_command(QStringLiteral("sphere"));
    sphere->point({0, 0, 0}, kTop);
    QCOMPARE(sphere->number(2.0), R::Done);
    QVERIFY(std::abs(shell_of(sphere->takeEntities()[0]).volume() - 4.0 / 3.0 * kPi * 8) < 1e-6);

    auto cyl = create_command(QStringLiteral("cylinder"));
    cyl->point({0, 0, 0}, kTop);
    cyl->number(1.0);
    QCOMPARE(cyl->point({5, 5, 3}, kTop), R::Done);  // height from the point: 3
    QVERIFY(std::abs(shell_of(cyl->takeEntities()[0]).volume() - kPi * 3) < 1e-6);

    auto cone = create_command(QStringLiteral("cone"));
    cone->point({0, 0, 0}, kTop);
    cone->number(3.0);
    QCOMPARE(cone->number(4.0), R::Done);
    QVERIFY(std::abs(shell_of(cone->takeEntities()[0]).volume() - kPi * 9 * 4 / 3.0) < 1e-6);

    auto torus = create_command(QStringLiteral("torus"));
    torus->point({0, 0, 0}, kTop);
    torus->number(5.0);
    QCOMPARE(torus->number(6.0), R::Rejected);  // tube radius >= major radius
    QCOMPARE(torus->number(1.0), R::Done);
    QVERIFY(std::abs(shell_of(torus->takeEntities()[0]).volume() - 2 * kPi * kPi * 5) < 1e-5);
  }

  void booleanDifferenceAndUndo() {
    Document doc;
    const auto ids = doc.addEntities(
        {nfcad::document::make_entity(nf::make_box({-5, -5, 0}, {10, 10, 4}), QStringLiteral("plate")),
         nfcad::document::make_entity(nf::make_cylinder({0, 0, -1}, {0, 0, 1}, 2.0, 6.0))},
        QStringLiteral("add"));
    auto c = create_command(QStringLiteral("difference"));
    QVERIFY(c->usesSelection() && c->immediate());
    QVERIFY(!c->begin({*doc.entity(ids[0])}));  // one solid only
    c = create_command(QStringLiteral("difference"));
    QVERIFY(c->begin({*doc.entity(ids[0]), *doc.entity(ids[1])}));
    QCOMPARE(c->finish(), R::Done);
    doc.apply(c->takeEdit(), c->title());
    QCOMPARE(doc.entities().size(), std::size_t{1});
    const Entity& result = doc.entities().begin()->second;
    QCOMPARE(result.name, QStringLiteral("plate"));
    QVERIFY2(std::abs(shell_of(result).volume() / (400.0 - kPi * 16) - 1.0) < 1e-7,  // volume integration: rel. 1e-8
             qPrintable(QString::number(shell_of(result).volume(), 'g', 12)));
    doc.undoStack()->undo();  // one step restores both inputs
    QCOMPARE(doc.entities().size(), std::size_t{2});
    QVERIFY(doc.entity(ids[0]) && doc.entity(ids[1]));
  }

  void moveRotateScaleMirror() {
    Document doc;
    const auto ids = doc.addEntities({nfcad::document::make_entity(nf::make_line({1, 0, 0}, {2, 0, 0}))},
                                     QStringLiteral("add"));
    auto end_of = [&](EntityId id) {
      const auto& c = std::get<nf::NurbsCurve>(*doc.entity(id)->geometry);
      return c.evaluate(c.domain_end());
    };
    run(doc, create_command(QStringLiteral("move")), entities(doc), [](Command& c) {
      c.point({0, 0, 0}, kTop);
      QCOMPARE(c.point({0, 5, 0}, kTop), R::Done);
    });
    QCOMPARE(doc.entities().size(), std::size_t{1});  // moved in place, same id
    QVERIFY(nf::distance(end_of(ids[0]), {2, 5, 0}) < 1e-12);
    doc.undoStack()->undo();
    QVERIFY(nf::distance(end_of(ids[0]), {2, 0, 0}) < 1e-12);

    run(doc, create_command(QStringLiteral("rotate")), entities(doc), [](Command& c) {
      c.point({0, 0, 0}, kTop);
      QCOMPARE(c.number(90.0), R::Done);
    });
    QVERIFY(nf::distance(end_of(ids[0]), {0, 2, 0}) < 1e-12);

    run(doc, create_command(QStringLiteral("rotate")), entities(doc), [](Command& c) {
      c.point({0, 0, 0}, kTop);
      c.point({0, 1, 0}, kTop);               // reference direction +y
      QCOMPARE(c.point({-1, 0, 0}, kTop), R::Done);  // to -x: +90°
    });
    QVERIFY(nf::distance(end_of(ids[0]), {-2, 0, 0}) < 1e-12);

    run(doc, create_command(QStringLiteral("scale")), entities(doc), [](Command& c) {
      c.point({0, 0, 0}, kTop);
      QCOMPARE(c.number(3.0), R::Done);
    });
    QVERIFY(nf::distance(end_of(ids[0]), {-6, 0, 0}) < 1e-12);

    run(doc, create_command(QStringLiteral("mirror")), entities(doc), [](Command& c) {
      c.point({0, 0, 0}, kTop);
      QCOMPARE(c.point({0, 1, 0}, kTop), R::Done);  // mirror at the y axis (copy)
    });
    QCOMPARE(doc.entities().size(), std::size_t{2});
    const auto copy = doc.entities().rbegin()->first;
    QVERIFY(nf::distance(end_of(copy), {6, 0, 0}) < 1e-12);
  }

  void copyAndArrays() {
    Document doc;
    doc.addEntities({nfcad::document::make_entity(nf::make_box({0, 0, 0}, {1, 1, 1}))},
                    QStringLiteral("add"));
    const auto one = entities(doc);
    run(doc, create_command(QStringLiteral("copy")), one, [](Command& c) {
      c.point({0, 0, 0}, kTop);
      QCOMPARE(c.point({5, 0, 0}, kTop), R::Continue);
      QCOMPARE(c.point({10, 0, 0}, kTop), R::Continue);
      QCOMPARE(c.finish(), R::Done);
    });
    QCOMPARE(doc.entities().size(), std::size_t{3});

    run(doc, create_command(QStringLiteral("array_linear")), one, [](Command& c) {
      QCOMPARE(c.number(4), R::Continue);
      c.point({0, 0, 0}, kTop);
      QCOMPARE(c.point({0, 2, 0}, kTop), R::Done);
    });
    QCOMPARE(doc.entities().size(), std::size_t{6});

    run(doc, create_command(QStringLiteral("array_grid")), one, [](Command& c) {
      c.number(2);
      c.number(3);
      c.point({0, 0, 0}, kTop);
      c.point({0, 0, 3}, kTop);
      QCOMPARE(c.point({3, 3, 0}, kTop), R::Done);
    });
    QCOMPARE(doc.entities().size(), std::size_t{6 + 5});

    run(doc, create_command(QStringLiteral("array_polar")), one, [](Command& c) {
      c.number(8);
      c.point({-5, 0, 0}, kTop);
      QCOMPARE(c.finish(), R::Done);  // Enter: 360°
    });
    QCOMPARE(doc.entities().size(), std::size_t{11 + 7});
    // the polar copy at 180° lies at x = -10 .. -11
    bool found = false;
    for (const auto& [id, e] : doc.entities()) {
      const double cx = shell_of(e).mass_properties().centroid.x;
      found = found || std::abs(cx - (-10.5)) < 1e-9;
    }
    QVERIFY(found);
  }

  void selectionCommandsNeedASelection() {
    auto c = create_command(QStringLiteral("move"));
    QVERIFY(!c->begin({}));
    QVERIFY(!c->message().isEmpty());
  }

  void angle() {
    auto c = create_command(QStringLiteral("angle"));
    c->point({0, 0, 0}, kTop);
    c->point({1, 0, 0}, kTop);
    QCOMPARE(c->point({1, 1, 0}, kTop), R::Done);
    QVERIFY(c->message().startsWith(QStringLiteral("Angle 45")));
  }

  void selectionOrder() {
    nfcad::document::Selection s;
    s.apply({{3, {}}}, nfcad::document::Selection::Mode::Replace);
    s.apply({{1, {}}}, nfcad::document::Selection::Mode::Add);
    s.apply({{2, {}}}, nfcad::document::Selection::Mode::Add);
    QCOMPARE(s.ordered().size(), std::size_t{3});
    QCOMPARE(s.ordered()[0].entity, nfcad::document::EntityId{3});
    QCOMPARE(s.ordered()[2].entity, nfcad::document::EntityId{2});
    s.apply({{1, {}}}, nfcad::document::Selection::Mode::Remove);
    QCOMPARE(s.ordered().size(), std::size_t{2});
  }

  void exportStepAndStl() {
    QTemporaryDir dir;
    const Entity box = nfcad::document::make_entity(nf::make_box({0, 0, 0}, {1, 2, 3}), QStringLiteral("b"));
    const Entity line = nfcad::document::make_entity(nf::make_line({0, 0, 0}, {1, 0, 0}));
    const QString step = dir.filePath(QStringLiteral("x.step"));
    QCOMPARE(nfcad::kernel::export_step(step, {&box, &line}), std::size_t{1});  // the line is skipped
    const auto back = nf::read_step(step.toStdString());
    QCOMPARE(back.size(), std::size_t{1});
    QVERIFY(std::abs(back[0].volume() - 6.0) < 1e-9);

    const auto mesh = nfcad::tessellation::tessellate(*box.geometry, nfcad::tessellation::TessParams{});
    const QString stl = dir.filePath(QStringLiteral("x.stl"));
    QVERIFY(nfcad::tessellation::write_stl(stl, {&mesh}).isEmpty());
    QCOMPARE(QFile(stl).size(), qint64(84 + 50 * mesh.triangleCount()));
  }
};

QTEST_GUILESS_MAIN(TestSolidTransformCommands)
#include "test_solid_transform_commands.moc"
