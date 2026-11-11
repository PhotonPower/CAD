#include "app/demo_results.hpp"

#include <QObject>
#include <cmath>
#include <random>

#include "render/scene.hpp"
#include "results/result_store.hpp"

namespace nfcad::app {

QString make_demo_results(results::ResultStore& store, const render::Scene& scene,
                          document::EntityId entity) {
  const auto it = scene.items().find(entity);
  if (it == scene.items().end() || !it->second.mesh || it->second.mesh->face_starts.size() < 2) {
    return QObject::tr("Select a surface or solid first");
  }
  const tessellation::DisplayMesh& m = *it->second.mesh;
  const QVector3D centre = m.bounds.center();
  const float r = std::max(m.bounds.radius(), 1e-3F);
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> unit(0.0F, 1.0F);

  // average normal of face 0 → the source sits above it
  QVector3D n;
  const std::uint32_t first = m.face_starts[0];
  const std::uint32_t last = m.face_starts[1];
  for (std::uint32_t k = first; k < last; ++k) {
    const auto v = tessellation::kVertexStride * m.indices[k];
    n += QVector3D(m.vertices[v + 3], m.vertices[v + 4], m.vertices[v + 5]);
  }
  n = n.lengthSquared() > 0 ? n.normalized() : QVector3D(0, 0, 1);
  const QVector3D source = centre + n * (2.0F * r);

  results::LineSet rays;
  rays.colormap = results::Colormap::Viridis;
  rays.unit = QStringLiteral("nm");
  const std::uint32_t triangles = (last - first) / 3;
  for (int k = 0; k < 600 && triangles > 0; ++k) {
    const std::uint32_t t = first + 3 * std::min<std::uint32_t>(triangles - 1, static_cast<std::uint32_t>(unit(rng) * triangles));
    float a = unit(rng);
    float b = unit(rng);
    if (a + b > 1.0F) {
      a = 1.0F - a;
      b = 1.0F - b;
    }
    const QVector3D p0 = m.vertex(m.indices[t]);
    const QVector3D hit = p0 + (m.vertex(m.indices[t + 1]) - p0) * a + (m.vertex(m.indices[t + 2]) - p0) * b;
    const QVector3D d = (hit - source).normalized();
    const QVector3D refl = d - 2.0F * QVector3D::dotProduct(d, n) * n;
    const QVector3D out = hit + refl * (1.5F * r);
    rays.starts.push_back(static_cast<std::uint32_t>(rays.pointCount()));
    for (const QVector3D& q : {source, hit, out}) rays.points.insert(rays.points.end(), {q.x(), q.y(), q.z()});
    const float wavelength = 450.0F + 200.0F * unit(rng);
    rays.values.insert(rays.values.end(), {wavelength, wavelength, wavelength});
  }
  rays.starts.push_back(static_cast<std::uint32_t>(rays.pointCount()));

  results::FaceField spot;
  spot.entity = entity;
  spot.face = 0;
  spot.width = 128;
  spot.height = 128;
  spot.unit = QStringLiteral("W/m²");
  spot.colormap = results::Colormap::Inferno;
  for (int j = 0; j < spot.height; ++j) {
    for (int i = 0; i < spot.width; ++i) {
      const double u = (i + 0.5) / spot.width - 0.55;
      const double v = (j + 0.5) / spot.height - 0.45;
      const double rho = std::hypot(u, v);
      spot.values.push_back(static_cast<float>(std::exp(-rho * rho / 0.01) +
                                               0.15 * std::pow(std::cos(40.0 * rho), 2) * std::exp(-rho * 4)));
    }
  }

  results::Curve merit;
  merit.x_label = QObject::tr("iteration");
  merit.y_label = QObject::tr("merit");
  for (int k = 0; k <= 40; ++k) {
    merit.x.push_back(k);
    merit.y.push_back(1.0 / (1.0 + 0.4 * k) + 0.02 * std::sin(k));
  }

  const results::ResultId set = store.createSet(QObject::tr("Demo run"), QStringLiteral("demo"));
  store.addLines(set, QObject::tr("Rays"), std::move(rays));
  store.addField(set, QObject::tr("Irradiance"), std::move(spot));
  store.addCurve(set, QObject::tr("Merit"), std::move(merit));
  return {};
}

}  // namespace nfcad::app
