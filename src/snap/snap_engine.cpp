#include "snap/snap_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "kernel/curve_features.hpp"

namespace nfcad::snap {

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr std::size_t kMaxIntersectionSegments = 48;

QVector3D q3(const nf::Vec3& p) {
  return {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
}
nf::Vec3 n3(const QVector3D& p) { return {p.x(), p.y(), p.z()}; }

/// Proper intersection of two 2D segments: parameters on both, or empty.
std::optional<std::pair<double, double>> segments_2d(const QPointF& a, const QPointF& b,
                                                     const QPointF& c, const QPointF& d) {
  const QPointF r = b - a;
  const QPointF s = d - c;
  const double den = r.x() * s.y() - r.y() * s.x();
  if (std::abs(den) < 1e-12) return std::nullopt;
  const QPointF ac = c - a;
  const double t = (ac.x() * s.y() - ac.y() * s.x()) / den;
  const double u = (ac.x() * r.y() - ac.y() * r.x()) / den;
  if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return std::nullopt;
  return std::make_pair(t, u);
}

std::pair<double, double> point_segment_2d(const QPointF& p, const QPointF& a, const QPointF& b) {
  const QPointF ab = b - a;
  const double len2 = QPointF::dotProduct(ab, ab);
  const double s = len2 > 0.0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
  const QPointF c = a + ab * s;
  return {std::hypot(p.x() - c.x(), p.y() - c.y()), s};
}

nf::Vec3 nearest_on(const std::vector<nf::NurbsCurve>& curves, const nf::Vec3& p) {
  nf::Vec3 best = p;
  double best_d = std::numeric_limits<double>::max();
  for (const nf::NurbsCurve& c : curves) {
    const nf::Vec3 q = kernel::closest_on_curve(c, p);
    const double d = nf::distance(q, p);
    if (d < best_d) {
      best_d = d;
      best = q;
    }
  }
  return best;
}

}  // namespace

void SnapEngine::clear() {
  caches_.clear();
  intersections_.clear();
}

const SnapEngine::Cache& SnapEngine::cache(const pick::Target& t) {
  const auto it = caches_.find(t.id);
  if (it != caches_.end() && it->second.geometry == t.geometry) return it->second;
  // geometry changed: drop its cached intersections
  for (auto x = intersections_.begin(); x != intersections_.end();) {
    x = (std::get<0>(x->first) == t.id || std::get<2>(x->first) == t.id) ? intersections_.erase(x)
                                                                           : std::next(x);
  }
  Cache c;
  c.geometry = t.geometry;
  const document::Geometry& g = *t.geometry;
  auto add_curve_features = [&](const nf::NurbsCurve& curve, bool edge) {
    const kernel::CurveFeatures f = kernel::curve_features(curve);
    if (!edge && f.start) c.fixed.push_back({*f.start, SnapType::End, false});
    if (!edge && f.end) c.fixed.push_back({*f.end, SnapType::End, false});
    if (f.mid) c.fixed.push_back({*f.mid, SnapType::Mid, edge});
    if (f.centre) c.fixed.push_back({*f.centre, SnapType::Centre, edge});
    for (const nf::Vec3& qd : f.quadrants) c.fixed.push_back({qd, SnapType::Quadrant, edge});
  };
  if (const auto* p = std::get_if<nf::Vec3>(&g)) {
    c.fixed.push_back({*p, SnapType::Point, false});
  } else if (const auto* curve = std::get_if<nf::NurbsCurve>(&g)) {
    c.curve_entity = true;
    c.curves = {{*curve}};
    add_curve_features(*curve, false);
  } else {
    c.curves = kernel::edge_curves(g);
    for (const auto& list : c.curves) {
      for (const nf::NurbsCurve& edge : list) add_curve_features(edge, true);
    }
    for (const nf::Vec3& corner : kernel::corners(g)) {
      c.fixed.push_back({corner, SnapType::FaceCorner, false});
    }
  }
  return caches_.insert_or_assign(t.id, std::move(c)).first->second;
}

std::vector<SnapResult> SnapEngine::query(const std::vector<pick::Target>& targets,
                                          const viewport::Camera& camera, const SnapQuery& q) {
  std::vector<SnapResult> out;
  const viewport::Ray ray = camera.pickRay(q.cursor_px);
  const QVector3D inv(ray.direction.x() != 0.0F ? 1.0F / ray.direction.x() : 1e30F,
                      ray.direction.y() != 0.0F ? 1.0F / ray.direction.y() : 1e30F,
                      ray.direction.z() != 0.0F ? 1.0F / ray.direction.z() : 1e30F);
  auto depth_of = [&](const nf::Vec3& p) {
    return QVector3D::dotProduct(q3(p) - ray.origin, ray.direction);
  };
  auto add = [&](const nf::Vec3& p, SnapType type, document::EntityId e,
                 document::EntityId other = 0, bool apparent = false) {
    if (!q.mask.test(type)) return;
    const auto s = camera.project(q3(p));
    if (!s) return;
    const auto d = static_cast<float>(std::hypot(s->x() - q.cursor_px.x(), s->y() - q.cursor_px.y()));
    if (d > q.radius_px) return;
    out.push_back({{p.x, p.y, p.z}, type, e, other, apparent, d, depth_of(p)});
  };
  const bool edges_on = q.mask.test(SnapType::FaceEdge);

  struct NearSeg {
    const pick::Target* target;
    const Cache* cache;
    std::uint32_t polyline;
    QVector3D a, b;
    QPointF sa, sb;
    double dist;
    double s;
  };
  std::vector<NearSeg> near;

  for (const pick::Target& t : targets) {
    const tessellation::DisplayMesh& m = *t.mesh;
    if (!m.bounds.valid() || !t.geometry) continue;
    const float inflate = q.radius_px * pick::farthest_world_per_pixel(camera, m.bounds);
    tessellation::Box3 grown = m.bounds;
    grown.min -= QVector3D(inflate, inflate, inflate);
    grown.max += QVector3D(inflate, inflate, inflate);
    if (tessellation::ray_box(ray.origin, inv, grown, kInf) < 0.0F) continue;

    const Cache& c = cache(t);
    for (const Fixed& f : c.fixed) {
      if (f.edge && !edges_on) continue;
      add(f.p, f.type, t.id);
    }
    if (m.segment_bvh.empty() || (!c.curve_entity && !edges_on)) continue;
    m.segment_bvh.raycast(ray.origin, ray.direction, inflate, kInf,
                          [&](std::uint32_t si, float& /*t_max*/) {
      const auto& seg = m.segments[si];
      if (seg.iso) return;
      const auto [a, b] = m.segmentPoints(seg);
      const auto sa = camera.project(a);
      const auto sb = camera.project(b);
      if (!sa || !sb) return;
      const auto [dist, s] = point_segment_2d(q.cursor_px, *sa, *sb);
      if (dist > q.radius_px) return;
      near.push_back({&t, &c, seg.polyline, a, b, *sa, *sb, dist, s});
    });
  }

  // ---- Near: exact foot point on the closest curve of each polyline
  if (q.mask.test(SnapType::Near)) {
    std::map<std::pair<document::EntityId, std::uint32_t>, const NearSeg*> best;
    for (const NearSeg& n : near) {
      auto& b = best[{n.target->id, n.polyline}];
      if (b == nullptr || n.dist < b->dist) b = &n;
    }
    for (const auto& [key, n] : best) {
      const nf::Vec3 approx = n3(n->a + (n->b - n->a) * static_cast<float>(n->s));
      const nf::Vec3 p = n->polyline < n->cache->curves.size()
                             ? nearest_on(n->cache->curves[n->polyline], approx)
                             : approx;
      add(p, SnapType::Near, n->target->id);
    }
  }

  // ---- Intersection: crossing display segments, refined with the exact curves
  if (q.mask.test(SnapType::Intersection)) {
    std::sort(near.begin(), near.end(), [](const NearSeg& a, const NearSeg& b) { return a.dist < b.dist; });
    if (near.size() > kMaxIntersectionSegments) near.resize(kMaxIntersectionSegments);
    const bool parallel = camera.projection() == viewport::Camera::Projection::Orthographic;
    for (std::size_t i = 0; i < near.size(); ++i) {
      for (std::size_t j = i + 1; j < near.size(); ++j) {
        const NearSeg& A = near[i];
        const NearSeg& B = near[j];
        if (A.target == B.target && A.polyline == B.polyline) continue;
        // edges of one face / shell only meet at its vertices (FaceCorner snaps)
        if (A.target == B.target && !A.cache->curve_entity) continue;
        const auto x = segments_2d(A.sa, A.sb, B.sa, B.sb);
        if (!x) continue;
        const QVector3D pa = A.a + (A.b - A.a) * static_cast<float>(x->first);
        const QVector3D pb = B.a + (B.b - B.a) * static_cast<float>(x->second);
        const float tol = 4.0F * camera.worldPerPixelAt(pa);
        const bool has_curves = A.polyline < A.cache->curves.size() &&
                                B.polyline < B.cache->curves.size();
        if ((pa - pb).length() < tol && has_curves) {
          // real intersection: exact points of the curve pair (cached)
          PairKey key{A.target->id, A.polyline, B.target->id, B.polyline};
          if (std::get<0>(key) > std::get<2>(key) ||
              (std::get<0>(key) == std::get<2>(key) && std::get<1>(key) > std::get<3>(key))) {
            key = {B.target->id, B.polyline, A.target->id, A.polyline};
          }
          auto it = intersections_.find(key);
          if (it == intersections_.end()) {
            std::vector<nf::Vec3> pts;
            for (const auto& ca : A.cache->curves[A.polyline]) {
              for (const auto& cb : B.cache->curves[B.polyline]) {
                const auto xs = kernel::curve_intersections(ca, cb);
                pts.insert(pts.end(), xs.begin(), xs.end());
              }
            }
            it = intersections_.emplace(key, std::move(pts)).first;
          }
          const nf::Vec3 approx = n3(pa);
          const nf::Vec3* bestp = nullptr;
          for (const nf::Vec3& p : it->second) {
            if (bestp == nullptr || nf::distance(p, approx) < nf::distance(*bestp, approx)) bestp = &p;
          }
          if (bestp != nullptr && nf::distance(*bestp, approx) < 10.0 * tol) {
            add(*bestp, SnapType::Intersection, A.target->id, B.target->id);
          }
        } else if (parallel) {
          // apparent intersection: the point on the curve nearer to the viewer
          const bool a_front = QVector3D::dotProduct(pa - pb, ray.direction) < 0.0F;
          const NearSeg& F = a_front ? A : B;
          const QVector3D pf = a_front ? pa : pb;
          const nf::Vec3 p = F.polyline < F.cache->curves.size()
                                 ? nearest_on(F.cache->curves[F.polyline], n3(pf))
                                 : n3(pf);
          add(p, SnapType::Intersection, A.target->id, B.target->id, true);
        }
      }
    }
  }

  // ---- Grid points of the construction plane
  if (q.mask.test(SnapType::Grid) && q.grid) {
    const auto& [plane, spacing] = *q.grid;
    if (const auto hit = viewport::intersect_plane(ray, plane.origin, plane.normal()); hit && spacing > 0.0F) {
      const QVector3D rel = *hit - plane.origin;
      const double u = std::round(QVector3D::dotProduct(rel, plane.u) / spacing) * spacing;
      const double v = std::round(QVector3D::dotProduct(rel, plane.v) / spacing) * spacing;
      const nf::Vec3 o = n3(plane.origin);
      add(o + n3(plane.u) * u + n3(plane.v) * v, SnapType::Grid, 0);
    }
  }

  // ---- hidden candidates (behind the face under the cursor)
  if (q.faces_occlude && !out.empty()) {
    if (const auto face = pick::pick_face(targets, camera, q.cursor_px)) {
      const float tol = 2.0F * q.radius_px * camera.worldPerPixelAt(face->point);
      std::erase_if(out, [&](const SnapResult& r) { return r.depth > face->depth + tol; });
    }
  }

  std::sort(out.begin(), out.end(), [](const SnapResult& a, const SnapResult& b) {
    const int ra = priority_rank(a.type);
    const int rb = priority_rank(b.type);
    if (ra != rb) return ra < rb;
    if (std::abs(a.distance_px - b.distance_px) > 0.5F) return a.distance_px < b.distance_px;
    return a.depth < b.depth;
  });
  // the same point found several times (shared corners, segment pairs): keep the first
  std::vector<SnapResult> unique;
  for (const SnapResult& r : out) {
    const bool dup = std::any_of(unique.begin(), unique.end(), [&](const SnapResult& u) {
      return u.type == r.type && (u.position() - r.position()).length() <=
                                     1e-6F * std::max(1.0F, u.position().length());
    });
    if (!dup) unique.push_back(r);
  }
  return unique;
}

}  // namespace nfcad::snap
