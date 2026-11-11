#include <QTest>
#include <cmath>
#include <numbers>
#include <nurbforge/nurbforge.hpp>

#include "tessellation/tessellator.hpp"

using nfcad::tessellation::DisplayMesh;
using nfcad::tessellation::TessParams;

namespace {

nf::Vec3 vertex(const DisplayMesh& m, std::size_t i) {
  const std::size_t k = nfcad::tessellation::kVertexStride * i;
  return {m.vertices[k], m.vertices[k + 1], m.vertices[k + 2]};
}
nf::Vec3 normal(const DisplayMesh& m, std::size_t i) {
  const std::size_t k = nfcad::tessellation::kVertexStride * i;
  return {m.vertices[k + 3], m.vertices[k + 4], m.vertices[k + 5]};
}

double mesh_area(const DisplayMesh& m) {
  double a = 0.0;
  for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
    const nf::Vec3 p0 = vertex(m, m.indices[t]);
    const nf::Vec3 p1 = vertex(m, m.indices[t + 1]);
    const nf::Vec3 p2 = vertex(m, m.indices[t + 2]);
    a += 0.5 * nf::norm(nf::cross(p1 - p0, p2 - p0));
  }
  return a;
}

}  // namespace

class TestTessellator : public QObject {
  Q_OBJECT
 private slots:
  void circleWithinChordTolerance() {
    const nf::NurbsCurve circle = nf::make_circle({1, 2, 3}, {0, 0, 1}, 10.0);
    TessParams p;
    p.chord = 0.01;
    const auto pts = nfcad::tessellation::tessellate_curve(circle, p);
    QVERIFY(pts.size() > 16);
    double max_sagitta = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
      // vertices on the circle
      QVERIFY(std::abs(nf::distance(pts[i], {1, 2, 3}) - 10.0) < 1e-6);
      if (i + 1 < pts.size()) {
        // sagitta of the chord = 10 - distance(centre, chord midpoint)
        const nf::Vec3 mid = (pts[i] + pts[i + 1]) * 0.5;
        max_sagitta = std::max(max_sagitta, 10.0 - nf::distance(mid, {1, 2, 3}));
      }
    }
    QVERIFY2(max_sagitta <= p.chord * 1.01, qPrintable(QString::number(max_sagitta)));
    QVERIFY(nf::distance(pts.front(), pts.back()) < 1e-9);  // closed
  }

  void lineIsOneSegment() {
    const auto pts = nfcad::tessellation::tessellate_curve(nf::make_line({0, 0, 0}, {5, 0, 0}),
                                                           TessParams{});
    QCOMPARE(pts.size(), std::size_t{2});
  }

  void sphereMeshOnSphereWithOutwardNormals() {
    const nf::Shell sphere = nf::make_sphere({0, 0, 0}, 5.0);
    TessParams p;
    p.chord = 0.005;
    const DisplayMesh m = nfcad::tessellation::tessellate(nfcad::document::Geometry(sphere), p);
    QVERIFY(m.triangleCount() > 100);
    for (std::size_t i = 0; i < m.vertexCount(); ++i) {
      QVERIFY(std::abs(nf::norm(vertex(m, i)) - 5.0) < 1e-4);
      const nf::Vec3 n = normal(m, i);
      QVERIFY(std::abs(nf::norm(n) - 1.0) < 1e-3);
      // radial (outward for a consistently oriented closed shell; sign not required here)
      QVERIFY(std::abs(nf::dot(n, vertex(m, i))) / 5.0 > 0.999);
    }
    const double exact = 4.0 * std::numbers::pi * 25.0;
    QVERIFY2(std::abs(mesh_area(m) - exact) / exact < 0.005,
             qPrintable(QString::number(mesh_area(m))));
    QVERIFY(!m.edges.empty());  // seam edges of the shell
    QVERIFY(m.bounds.valid());
    QVERIFY(std::abs(m.bounds.max.x() - 5.0F) < 1e-3F);
  }

  void trimmedFaceWithHoleHasCorrectArea() {
    // Plate 20 x 20 with a hole of radius 5: top face area 400 - 25 pi.
    const nf::Shell box = nf::make_box({-10, -10, 0}, {20, 20, 8});
    const nf::Shell cyl = nf::make_cylinder({0, 0, -1}, {0, 0, 1}, 5.0, 10.0);
    const nf::Shell plate = nf::boolean(box, cyl, nf::BooleanOp::Difference);
    bool found = false;
    for (const nf::TrimmedSurface& f : plate.faces()) {
      if (f.holes().empty()) continue;
      DisplayMesh m;
      TessParams p;
      p.chord = 0.002;
      nfcad::tessellation::tessellate_face(f, p, m, true);
      const double exact = f.area();
      QVERIFY2(std::abs(mesh_area(m) - exact) / exact < 0.002,
               qPrintable(QStringLiteral("%1 vs %2").arg(mesh_area(m)).arg(exact)));
      QCOMPARE(m.edges.size(), std::size_t{2});  // outer loop + hole
      found = true;
    }
    QVERIFY(found);
  }

  void untrimmedSurfaceIsGrid() {
    const nf::NurbsSurface s = nf::make_rectangle({0, 0, 0}, {4, 0, 0}, {0, 3, 0});
    const DisplayMesh m = nfcad::tessellation::tessellate(nfcad::document::Geometry(s), TessParams{});
    QCOMPARE(m.triangleCount(), std::size_t{2});  // flat, degree 1: one cell
    QVERIFY(std::abs(mesh_area(m) - 12.0) < 1e-9);
    QCOMPARE(m.edges.size(), std::size_t{1});
  }

  void uvCoordinatesCoverTheFace() {
    // plane 4 x 3: (u, v) runs over [0, 1]^2 and maps linearly to the corners
    const nf::NurbsSurface s = nf::make_rectangle({0, 0, 0}, {4, 0, 0}, {0, 3, 0});
    const DisplayMesh m = nfcad::tessellation::tessellate(nfcad::document::Geometry(s), TessParams{});
    for (std::uint32_t i = 0; i < m.vertexCount(); ++i) {
      const auto [u, v] = m.uv(i);
      const QVector3D p = m.vertex(i);
      QVERIFY(std::abs(p.x() - 4.0F * u) < 1e-5F);
      QVERIFY(std::abs(p.y() - 3.0F * v) < 1e-5F);
    }
    // trimmed face (plate with a hole): every vertex has (u, v) in [0, 1]
    const nf::Shell plate = nf::boolean(nf::make_box({-10, -10, 0}, {20, 20, 8}),
                                        nf::make_cylinder({0, 0, -1}, {0, 0, 1}, 5.0, 10.0),
                                        nf::BooleanOp::Difference);
    const DisplayMesh pm = nfcad::tessellation::tessellate(nfcad::document::Geometry(plate), TessParams{});
    for (std::uint32_t i = 0; i < pm.vertexCount(); ++i) {
      const auto [u, v] = pm.uv(i);
      QVERIFY(u >= -1e-6F && u <= 1.0F + 1e-6F && v >= -1e-6F && v <= 1.0F + 1e-6F);
    }
  }

  void pointEntity() {
    const DisplayMesh m =
        nfcad::tessellation::tessellate(nfcad::document::Geometry(nf::Vec3{1, 2, 3}), TessParams{});
    QCOMPARE(m.points.size(), std::size_t{3});
    QVERIFY(m.bounds.valid());
  }
};

QTEST_GUILESS_MAIN(TestTessellator)
#include "test_tessellator.moc"
