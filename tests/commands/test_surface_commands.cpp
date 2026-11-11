#include <QTest>
#include <cmath>
#include <numbers>
#include <nurbforge/analysis/properties.hpp>
#include <nurbforge/nurbforge.hpp>

#include "commands/command.hpp"

using nfcad::commands::Command;
using nfcad::commands::create_command;
using nfcad::commands::PlaneFrame;
using nfcad::document::Entity;
using nfcad::document::make_entity;
using R = Command::Result;

namespace {
const PlaneFrame kTop{};
constexpr double kPi = std::numbers::pi;

const nf::NurbsSurface& surface(const Entity& e) { return std::get<nf::NurbsSurface>(*e.geometry); }
const nf::NurbsCurve& curve(const Entity& e) { return std::get<nf::NurbsCurve>(*e.geometry); }

std::unique_ptr<Command> started(const QString& id, const std::vector<Entity>& sel) {
  auto c = create_command(id);
  if (!c->begin(sel)) qWarning("%s", qPrintable(c->message()));
  return c;
}
}  // namespace

class TestSurfaceCommands : public QObject {
  Q_OBJECT
 private slots:
  void splineThroughPoints() {
    auto c = create_command(QStringLiteral("spline"));
    for (const nf::Vec3& p : {nf::Vec3{0, 0, 0}, {1, 2, 0}, {3, 1, 0}, {4, 4, 1}}) {
      c->point({p.x, p.y, p.z}, kTop);
    }
    QCOMPARE(c->finish(), R::Done);
    const auto e = c->takeEntities();
    const auto& s = curve(e[0]);
    QCOMPARE(s.degree(), std::size_t{3});
    QVERIFY(nf::distance(s.evaluate(s.domain_end()), {4, 4, 1}) < 1e-12);
  }

  void planeSurface() {
    auto c = create_command(QStringLiteral("plane"));
    c->point({0, 0, 0}, kTop);
    QCOMPARE(c->point({3, 2, 7}, kTop), R::Done);
    QVERIFY(std::abs(nf::area(surface(c->takeEntities()[0])) - 6.0) < 1e-12);
  }

  void extrudeCircleIsCylinderMantle() {
    auto c = started(QStringLiteral("extrude"),
                     {make_entity(nf::make_circle({0, 0, 0}, {0, 0, 1}, 2.0))});
    QCOMPARE(c->number(5.0), R::Done);  // along the plane normal
    QVERIFY(std::abs(nf::area(surface(c->takeEntities()[0])) - 2 * kPi * 2 * 5) < 1e-6);
    c = started(QStringLiteral("extrude"), {make_entity(nf::make_line({0, 0, 0}, {1, 0, 0}))});
    c->point({0, 0, 0}, kTop);
    QCOMPARE(c->point({0, 3, 4}, kTop), R::Done);  // by two points
    QVERIFY(std::abs(nf::area(surface(c->takeEntities()[0])) - 5.0) < 1e-12);
  }

  void revolveLineIsCone() {
    // line from (1,0,0) to (0,0,1) about z: cone mantle, area pi r s = pi * 1 * sqrt(2)
    auto c = started(QStringLiteral("revolve"), {make_entity(nf::make_line({1, 0, 0}, {0, 0, 1}))});
    c->point({0, 0, 0}, kTop);
    c->point({0, 0, 1}, kTop);
    QCOMPARE(c->finish(), R::Done);  // 360°
    QVERIFY(std::abs(nf::area(surface(c->takeEntities()[0])) - kPi * std::sqrt(2.0)) < 1e-6);
    c = started(QStringLiteral("revolve"), {make_entity(nf::make_line({1, 0, 0}, {1, 0, 1}))});
    c->point({0, 0, 0}, kTop);
    c->point({0, 0, 1}, kTop);
    QCOMPARE(c->number(90.0), R::Done);  // quarter cylinder
    QVERIFY(std::abs(nf::area(surface(c->takeEntities()[0])) - kPi / 2) < 1e-6);
  }

  void loftSweepCoons() {
    const Entity c0 = make_entity(nf::make_circle({0, 0, 0}, {0, 0, 1}, 1.0));
    const Entity c1 = make_entity(nf::make_circle({0, 0, 2}, {0, 0, 1}, 1.0));
    auto loft = started(QStringLiteral("loft"), {c0, c1});
    QVERIFY(loft->immediate());
    QCOMPARE(loft->finish(), R::Done);
    QVERIFY(std::abs(nf::area(surface(loft->takeEntities()[0])) - 2 * kPi * 2) < 1e-6);
    QVERIFY(!create_command(QStringLiteral("loft"))->begin({c0}));  // one section only

    const Entity profile = make_entity(nf::make_circle({0, 0, 0}, {1, 0, 0}, 0.5));
    const Entity rail = make_entity(nf::make_line({0, 0, 0}, {4, 0, 0}));
    auto sweep = started(QStringLiteral("sweep1"), {profile, rail});
    QCOMPARE(sweep->finish(), R::Done);
    QVERIFY(std::abs(nf::area(surface(sweep->takeEntities()[0])) - 2 * kPi * 0.5 * 4) < 1e-4);

    auto coons = started(QStringLiteral("coons"),
                         {make_entity(nf::make_line({0, 0, 0}, {2, 0, 0})),
                          make_entity(nf::make_line({0, 3, 0}, {2, 3, 0})),
                          make_entity(nf::make_line({0, 0, 0}, {0, 3, 0})),
                          make_entity(nf::make_line({2, 0, 0}, {2, 3, 0}))});
    QCOMPARE(coons->finish(), R::Done);
    QVERIFY(std::abs(nf::area(surface(coons->takeEntities()[0])) - 6.0) < 1e-9);
  }

  void gordonFromUnorderedNetwork() {
    // 3 x 3 straight lines in the plane z = 0, given in mixed order
    std::vector<Entity> net;
    for (const double x : {2.0, 0.0, 1.0}) net.push_back(make_entity(nf::make_line({x, 0, 0}, {x, 2, 0})));
    for (const double y : {1.0, 2.0, 0.0}) net.push_back(make_entity(nf::make_line({0, y, 0}, {2, y, 0})));
    std::swap(net[1], net[4]);  // interleave the families
    auto g = started(QStringLiteral("gordon"), net);
    QVERIFY2(g->message().isEmpty(), qPrintable(g->message()));
    QCOMPARE(g->finish(), R::Done);
    QVERIFY(std::abs(nf::area(surface(g->takeEntities()[0])) - 4.0) < 1e-9);
  }

  void offsetCurveSideByClick() {
    auto c = started(QStringLiteral("offset_curve"),
                     {make_entity(nf::make_circle({0, 0, 0}, {0, 0, 1}, 3.0))});
    QCOMPARE(c->number(1.0), R::Continue);
    QCOMPARE(c->point({5, 0, 0}, kTop), R::Done);  // outside the circle
    const auto e = c->takeEntities();
    QVERIFY(std::abs(nf::norm(curve(e[0]).evaluate(0.37)) - 4.0) < 1e-6);
  }

  void filletCurvesAndCorners() {
    const Entity a = make_entity(nf::make_line({-5, 0, 0}, {0, 0, 0}));
    const Entity b = make_entity(nf::make_line({0, 0, 0}, {0, 5, 0}));
    auto c = started(QStringLiteral("fillet_curve"), {a, b});
    QCOMPARE(c->number(1.0), R::Done);
    const auto edit = c->takeEdit();
    QCOMPARE(edit.add.size(), std::size_t{1});      // the arc
    QCOMPARE(edit.replace.size(), std::size_t{2});  // both lines trimmed
    const auto& arc = curve(edit.add[0]);
    QVERIFY(std::abs(nf::arc_length(arc) - kPi / 2) < 1e-9);

    c = started(QStringLiteral("fillet_curve"),
                {make_entity(nf::make_rectangle_curve({0, 0, 0}, {4, 0, 0}, {0, 3, 0}))});
    QCOMPARE(c->number(0.5), R::Done);
    const auto rounded = c->takeEdit();
    QCOMPARE(rounded.replace.size(), std::size_t{1});
    const auto& rr = std::get<nf::NurbsCurve>(*rounded.replace[0].second);
    QVERIFY(std::abs(nf::arc_length(rr) - (14.0 - 8 * 0.5 + 2 * kPi * 0.5)) < 1e-6);
  }

  void offsetSurfaceOfSphereFace() {
    const auto sphere = nf::make_sphere({0, 0, 0}, 2.0);
    const Entity e = make_entity(sphere.faces()[0].surface());
    auto c = started(QStringLiteral("offset_surface"), {e});
    QCOMPARE(c->number(0.5), R::Done);
    const auto made = c->takeEntities();
    const auto& s = surface(made[0]);
    const double r = nf::norm(s.evaluate(0.3, 0.4));
    QVERIFY2(std::abs(r - 2.5) < 1e-4 || std::abs(r - 1.5) < 1e-4, qPrintable(QString::number(r)));
  }

  void filletSurfacesOfTwoPlanes() {
    const Entity floor = make_entity(nf::make_rectangle({-5, -5, 0}, {10, 0, 0}, {0, 10, 0}));
    const Entity wall = make_entity(nf::make_rectangle({0, -5, -5}, {0, 10, 0}, {0, 0, 10}));
    auto c = started(QStringLiteral("fillet_surface"), {floor, wall});
    QCOMPARE(c->number(1.0), R::Done);
    const auto made = c->takeEntities();
    const auto& f = surface(made[0]);
    // a quarter (or three-quarter) cylinder of radius 1 around a line parallel to y
    const nf::Vec3 p = f.evaluate(0.5, 0.5);
    QVERIFY(std::abs(std::hypot(std::abs(p.x) - 1.0, std::abs(p.z) - 1.0) - 1.0) < 1e-4);
  }

  void wrongSelections() {
    QVERIFY(!create_command(QStringLiteral("extrude"))->begin({}));
    QVERIFY(!create_command(QStringLiteral("sweep2"))->begin(
        {make_entity(nf::make_line({0, 0, 0}, {1, 0, 0}))}));
    QVERIFY(!create_command(QStringLiteral("fillet_surface"))->begin(
        {make_entity(nf::make_box({0, 0, 0}, {1, 1, 1}))}));
  }
};

QTEST_GUILESS_MAIN(TestSurfaceCommands)
#include "test_surface_commands.moc"
