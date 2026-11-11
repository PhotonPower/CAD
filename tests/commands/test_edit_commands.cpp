#include <QTest>
#include <cmath>
#include <numbers>
#include <nurbforge/analysis/properties.hpp>
#include <nurbforge/nurbforge.hpp>

#include "commands/command.hpp"
#include "kernel/geometry_ops.hpp"

using nfcad::commands::Command;
using nfcad::commands::create_command;
using nfcad::commands::PlaneFrame;
using nfcad::document::Entity;
using nfcad::document::make_entity;
using R = Command::Result;

namespace {
const PlaneFrame kTop{};
constexpr double kPi = std::numbers::pi;

Entity with_id(Entity e, nfcad::document::EntityId id) {
  e.id = id;
  return e;
}
}  // namespace

class TestEditCommands : public QObject {
  Q_OBJECT
 private slots:
  void joinCurvesInAnyOrderAndDirection() {
    const Entity a = with_id(make_entity(nf::make_line({0, 0, 0}, {1, 0, 0})), 1);
    const Entity b = with_id(make_entity(nf::make_line({1, 1, 0}, {1, 0, 0})), 2);  // reversed
    const Entity c = with_id(make_entity(nf::make_line({1, 1, 0}, {0, 1, 0})), 3);
    auto j = create_command(QStringLiteral("join"));
    QVERIFY2(j->begin({a, c, b}), qPrintable(j->message()));
    const auto e = j->takeEdit();
    QCOMPARE(e.replace.size(), std::size_t{1});
    QCOMPARE(e.remove.size(), std::size_t{2});
    QVERIFY(std::abs(nf::arc_length(std::get<nf::NurbsCurve>(*e.replace[0].second)) - 3.0) < 1e-12);
    QVERIFY(!create_command(QStringLiteral("join"))->begin(
        {a, with_id(make_entity(nf::make_line({5, 5, 0}, {6, 6, 0})), 4)}));  // gap
  }

  void joinFacesIntoSolidAndExplode() {
    const nf::Shell box = nf::make_box({0, 0, 0}, {1, 2, 3});
    std::vector<Entity> faces;
    for (std::size_t i = 0; i < box.faces().size(); ++i) {
      faces.push_back(with_id(make_entity(box.faces()[i]), i + 1));
    }
    auto j = create_command(QStringLiteral("join"));
    QVERIFY(j->begin(faces));
    const auto e = j->takeEdit();
    const auto& shell = std::get<nf::Shell>(*e.replace[0].second);
    QVERIFY(shell.is_closed());
    QVERIFY(std::abs(std::abs(shell.volume()) - 6.0) < 1e-9);

    auto x = create_command(QStringLiteral("explode"));
    QVERIFY(x->begin({with_id(make_entity(box), 9)}));
    const auto ex = x->takeEdit();
    QCOMPARE(ex.add.size(), std::size_t{6});
    QCOMPARE(ex.remove.size(), std::size_t{1});

    auto xp = create_command(QStringLiteral("explode"));
    QVERIFY(xp->begin({with_id(make_entity(nf::make_polyline({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}})), 3)}));
    QCOMPARE(xp->takeEdit().add.size(), std::size_t{3});
  }

  void capExtrudedCircleIntoCylinder() {
    const nf::NurbsSurface mantle = nf::extrude(nf::make_circle({0, 0, 0}, {0, 0, 1}, 2.0), {0, 0, 5});
    auto c = create_command(QStringLiteral("cap"));
    QVERIFY2(c->begin({with_id(make_entity(mantle), 1)}), qPrintable(c->message()));
    const auto e = c->takeEdit();
    const auto& shell = std::get<nf::Shell>(*e.replace[0].second);
    QVERIFY(shell.is_closed());
    QCOMPARE(shell.faces().size(), std::size_t{3});
    QVERIFY2(std::abs(std::abs(shell.volume()) - kPi * 4 * 5) < 1e-6, qPrintable(QString::number(shell.volume())));
  }

  void splitAndTrimCurve() {
    const Entity line = with_id(make_entity(nf::make_line({0, 0, 0}, {10, 0, 0})), 1);
    auto s = create_command(QStringLiteral("split"));
    QVERIFY(s->begin({line}));
    QCOMPARE(s->point({4, 1, 0}, kTop), R::Done);
    const auto e = s->takeEdit();
    QCOMPARE(e.add.size(), std::size_t{2});
    QVERIFY(std::abs(nf::arc_length(std::get<nf::NurbsCurve>(*e.add[0].geometry)) - 4.0) < 1e-9);

    const Entity cutter = with_id(make_entity(nf::make_line({3, -1, 0}, {3, 1, 0})), 2);
    const Entity cutter2 = with_id(make_entity(nf::make_line({7, -1, 0}, {7, 1, 0})), 3);
    auto t = create_command(QStringLiteral("trim"));
    QVERIFY(t->begin({line, cutter, cutter2}));
    QCOMPARE(t->point({5, 0.2, 0}, kTop), R::Done);  // remove the middle part
    const auto te = t->takeEdit();
    QCOMPARE(te.add.size(), std::size_t{2});
    double total = 0;
    for (const Entity& p : te.add) total += nf::arc_length(std::get<nf::NurbsCurve>(*p.geometry));
    QVERIFY(std::abs(total - 6.0) < 1e-9);
  }

  void splitAndTrimSurface() {
    const Entity plane = with_id(make_entity(nf::make_rectangle({-5, -5, 0}, {10, 0, 0}, {0, 10, 0})), 1);
    const Entity wall = with_id(make_entity(nf::make_rectangle({1, -6, -1}, {0, 12, 0}, {0, 0, 2})), 2);
    auto s = create_command(QStringLiteral("split"));
    QVERIFY2(s->begin({plane, wall}), qPrintable(s->message()));
    QVERIFY(s->immediate());
    QCOMPARE(s->takeEdit().add.size(), std::size_t{2});

    auto t = create_command(QStringLiteral("trim"));
    QVERIFY(t->begin({plane, wall}));
    QCOMPARE(t->point({3, 0, 0}, kTop), R::Done);  // remove the part x > 1
    const auto e = t->takeEdit();
    QCOMPARE(e.add.size(), std::size_t{1});
    QVERIFY(std::abs(std::get<nf::TrimmedSurface>(*e.add[0].geometry).area() - 60.0) < 1e-6);
  }

  void analysis() {
    auto a = create_command(QStringLiteral("area"));
    QVERIFY(a->begin({make_entity(nf::make_box({0, 0, 0}, {1, 2, 3})),
                      make_entity(nf::make_line({0, 0, 0}, {0, 0, 4}))}));
    QVERIFY(a->message().contains(QStringLiteral("Area 22")));
    QVERIFY(a->message().contains(QStringLiteral("Length 4")));

    auto m = create_command(QStringLiteral("volume"));
    QVERIFY(m->begin({make_entity(nf::make_box({0, 0, 0}, {2, 2, 2}))}));
    QVERIFY(m->message().startsWith(QStringLiteral("Volume 8   centroid (1, 1, 1)")));

    auto k = create_command(QStringLiteral("curvature"));
    QVERIFY(k->begin({make_entity(nf::make_circle({0, 0, 0}, {0, 0, 1}, 4.0))}));
    QCOMPARE(k->point({4, 0.1, 0}, kTop), R::Done);
    QVERIFY(k->message().contains(QStringLiteral("radius 4")));

    auto x = create_command(QStringLiteral("intersect"));
    QVERIFY(x->begin({make_entity(nf::make_sphere({0, 0, 0}, 2.0)),
                      make_entity(nf::make_rectangle({-5, -5, 1}, {10, 0, 0}, {0, 10, 0}))}));
    const auto xe = x->takeEdit();
    QVERIFY(!xe.add.empty());
    // the section circle z = 1 has radius sqrt(3)
    const auto& circle = std::get<nf::NurbsCurve>(*xe.add[0].geometry);
    const auto p = circle.evaluate(0.5 * (circle.domain_start() + circle.domain_end()));
    QVERIFY(std::abs(std::hypot(p.x, p.y) - std::sqrt(3.0)) < 1e-5);
    QVERIFY(std::abs(p.z - 1.0) < 1e-6);
  }
};

QTEST_GUILESS_MAIN(TestEditCommands)
#include "test_edit_commands.moc"
