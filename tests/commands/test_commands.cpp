#include <QTest>
#include <cmath>
#include <numbers>
#include <nurbforge/nurbforge.hpp>

#include "commands/command.hpp"
#include "commands/input_parser.hpp"

using nfcad::commands::Command;
using nfcad::commands::create_command;
using nfcad::commands::parse_input;
using nfcad::commands::ParsedInput;
using nfcad::commands::PlaneFrame;
using nfcad::commands::Point3;
using R = Command::Result;

namespace {

const PlaneFrame kTop{};                                         // XY
const PlaneFrame kFront{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}};  // XZ

const nf::NurbsCurve& curve_of(const std::vector<nfcad::document::Entity>& e) {
  return std::get<nf::NurbsCurve>(*e.front().geometry);
}

bool near(const Point3& a, const Point3& b, double tol = 1e-12) {
  return std::abs(a.x - b.x) < tol && std::abs(a.y - b.y) < tol && std::abs(a.z - b.z) < tol;
}

}  // namespace

class TestCommands : public QObject {
  Q_OBJECT
 private slots:
  // ---- input parser
  void parseAbsoluteAndPlane() {
    auto r = parse_input(QStringLiteral(" 1.5, -2, 3 "), kFront, std::nullopt);
    QCOMPARE(r.kind, ParsedInput::Kind::Point);
    QVERIFY(near(r.point, {1.5, -2, 3}));  // three values: world coordinates
    r = parse_input(QStringLiteral("4,5"), kFront, std::nullopt);
    QVERIFY(near(r.point, {4, 0, 5}));  // two values: in the front plane (x, z)
  }

  void parseRelativeAndPolar() {
    const Point3 base{10, 10, 0};
    auto r = parse_input(QStringLiteral("@1,2,3"), kTop, base);
    QVERIFY(near(r.point, {11, 12, 3}));
    r = parse_input(QStringLiteral("@3,4"), kTop, base);
    QVERIFY(near(r.point, {13, 14, 0}));
    r = parse_input(QStringLiteral("@10<90"), kTop, base);
    QVERIFY(near(r.point, {10, 20, 0}, 1e-12));
    r = parse_input(QStringLiteral("2<180"), kTop, base);  // absolute polar from the origin
    QVERIFY(near(r.point, {-2, 0, 0}, 1e-12));
  }

  void parseNumbersNamesErrors() {
    auto r = parse_input(QStringLiteral("12.5"), kTop, std::nullopt);
    QCOMPARE(r.kind, ParsedInput::Kind::Number);
    QCOMPARE(r.number, 12.5);
    r = parse_input(QStringLiteral("Circle"), kTop, std::nullopt);
    QCOMPARE(r.kind, ParsedInput::Kind::Name);
    QCOMPARE(r.name, QStringLiteral("circle"));
    QCOMPARE(parse_input(QStringLiteral("@1,2"), kTop, std::nullopt).kind, ParsedInput::Kind::Invalid);
    QCOMPARE(parse_input(QStringLiteral("1,x"), kTop, std::nullopt).kind, ParsedInput::Kind::Invalid);
    QCOMPARE(parse_input(QStringLiteral("1,2,3,4"), kTop, std::nullopt).kind, ParsedInput::Kind::Invalid);
    QCOMPARE(parse_input(QStringLiteral("  "), kTop, std::nullopt).kind, ParsedInput::Kind::Empty);
  }

  // ---- commands
  void factory() {
    QVERIFY(create_command(QStringLiteral("L")));
    QCOMPARE(create_command(QStringLiteral("pl"))->id(), QStringLiteral("polyline"));
    QVERIFY(!create_command(QStringLiteral("nonsense")));
    QCOMPARE(nfcad::commands::command_ids().size(), 10);
  }

  void line() {
    auto c = create_command(QStringLiteral("line"));
    QCOMPARE(c->point({0, 0, 0}, kTop), R::Continue);
    QVERIFY(c->basePoint());
    QCOMPARE(c->preview({1, 1, 0}, kTop).size(), std::size_t{1});
    QCOMPARE(c->point({0, 0, 0}, kTop), R::Rejected);  // zero length
    QCOMPARE(c->point({3, 4, 0}, kTop), R::Done);
    const auto e = c->takeEntities();
    QCOMPARE(e.size(), std::size_t{1});
    QVERIFY(nf::distance(curve_of(e).evaluate(curve_of(e).domain_end()), {3, 4, 0}) < 1e-15);
  }

  void polylineCloseAndFinish() {
    auto c = create_command(QStringLiteral("polyline"));
    QCOMPARE(c->finish(), R::Cancelled);
    c = create_command(QStringLiteral("polyline"));
    c->point({0, 0, 0}, kTop);
    c->point({4, 0, 0}, kTop);
    c->point({4, 3, 0}, kTop);
    QCOMPARE(c->point({0, 0, 0}, kTop), R::Done);  // back to the start: closed
    const auto closed = c->takeEntities();
    const auto& pl = curve_of(closed);
    QCOMPARE(pl.num_control_points(), std::size_t{4});
    c = create_command(QStringLiteral("polyline"));
    c->point({0, 0, 0}, kTop);
    c->point({1, 0, 0}, kTop);
    QCOMPARE(c->finish(), R::Done);
  }

  void rectangleInFrontPlane() {
    auto c = create_command(QStringLiteral("rectangle"));
    c->point({1, 0, 1}, kFront);
    QCOMPARE(c->point({5, 7, 3}, kFront), R::Done);  // y offset ignored: rectangle in the plane
    const auto rect = c->takeEntities();
    const auto& r = curve_of(rect);
    for (const auto& p : r.control_points()) QCOMPARE(p.y, 0.0);
    QVERIFY(nf::distance(r.control_points()[2], {5, 0, 3}) < 1e-12);
  }

  void circleByPointAndNumber() {
    auto c = create_command(QStringLiteral("circle"));
    QCOMPARE(c->number(3.0), R::Rejected);  // no centre yet
    c->point({1, 1, 0}, kTop);
    QCOMPARE(c->number(2.5), R::Done);
    const auto made = c->takeEntities();
    const auto& circle = curve_of(made);
    QVERIFY(std::abs(nf::distance(circle.evaluate(0.3), {1, 1, 0}) - 2.5) < 1e-12);
    c = create_command(QStringLiteral("circle"));
    c->point({0, 0, 0}, kTop);
    QCOMPARE(c->point({3, 4, 9}, kTop), R::Done);  // radius measured in the plane: 5
    QVERIFY(std::abs(nf::norm(curve_of(c->takeEntities()).evaluate(0.7)) - 5.0) < 1e-12);
  }

  void circle3pRejectsCollinear() {
    auto c = create_command(QStringLiteral("c3"));
    c->point({0, 0, 0}, kTop);
    c->point({1, 0, 0}, kTop);
    QCOMPARE(c->point({2, 0, 0}, kTop), R::Rejected);
    QCOMPARE(c->point({1, 1, 0}, kTop), R::Done);
  }

  void arcThroughPoint() {
    auto c = create_command(QStringLiteral("arc"));
    c->point({1, 0, 0}, kTop);   // start
    c->point({-1, 0, 0}, kTop);  // end
    QCOMPARE(c->point({0, 1, 0}, kTop), R::Done);  // through: upper half circle
    const auto made = c->takeEntities();
    const auto& arc = curve_of(made);
    QVERIFY(nf::distance(arc.evaluate(arc.domain_start()), {1, 0, 0}) < 1e-12);
    QVERIFY(nf::distance(arc.evaluate(arc.domain_end()), {-1, 0, 0}) < 1e-12);
    const auto mid = arc.evaluate(0.5 * (arc.domain_start() + arc.domain_end()));
    QVERIFY(mid.y > 0.99);
  }

  void polygonSidesAndRadius() {
    auto c = create_command(QStringLiteral("polygon"));
    QCOMPARE(c->number(2.5), R::Rejected);
    QCOMPARE(c->number(5), R::Continue);
    c->point({0, 0, 0}, kTop);
    QCOMPARE(c->point({2, 0, 0}, kTop), R::Done);
    const auto made = c->takeEntities();
    const auto& pg = curve_of(made);
    QCOMPARE(pg.num_control_points(), std::size_t{6});  // 5 corners + closing point
    QVERIFY(nf::distance(pg.control_points()[0], {2, 0, 0}) < 1e-12);
  }

  void helix() {
    auto c = create_command(QStringLiteral("helix"));
    QCOMPARE(c->number(3), R::Continue);  // turns
    c->point({0, 0, 0}, kTop);
    c->point({0, 0, 6}, kTop);
    QVERIFY(!c->preview({2, 0, 0}, kTop).empty());
    QCOMPARE(c->number(2.0), R::Done);  // radius
    const auto made = c->takeEntities();
    const auto& h = curve_of(made);
    const auto end = h.evaluate(h.domain_end());
    QVERIFY(std::abs(end.z - 6.0) < 1e-6);
    QVERIFY(std::abs(std::hypot(end.x, end.y) - 2.0) < 1e-6);
  }

  void distanceMessage() {
    auto c = create_command(QStringLiteral("distance"));
    c->point({0, 0, 0}, kTop);
    QCOMPARE(c->point({3, 4, 12}, kTop), R::Done);
    QVERIFY(c->message().startsWith(QStringLiteral("Distance 13")));
    QVERIFY(c->takeEntities().empty());
  }

  void pointCommandKeepsGoing() {
    auto c = create_command(QStringLiteral("point"));
    QCOMPARE(c->point({1, 2, 3}, kTop), R::Continue);
    QCOMPARE(c->takeEntities().size(), std::size_t{1});
    QCOMPARE(c->point({4, 5, 6}, kTop), R::Continue);
    QCOMPARE(c->finish(), R::Done);
  }
};

QTEST_GUILESS_MAIN(TestCommands)
#include "test_commands.moc"
