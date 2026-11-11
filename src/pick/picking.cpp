#include "pick/picking.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace nfcad::pick {

namespace {

using document::SelectionItem;
using document::SubElement;
using tessellation::Box3;
using tessellation::DisplayMesh;

constexpr float kInf = std::numeric_limits<float>::infinity();

/// Möller–Trumbore; returns t > 0 or a negative value.
float ray_triangle(const QVector3D& o, const QVector3D& d, const QVector3D& a, const QVector3D& b,
                   const QVector3D& c) {
  const QVector3D e1 = b - a;
  const QVector3D e2 = c - a;
  const QVector3D p = QVector3D::crossProduct(d, e2);
  const float det = QVector3D::dotProduct(e1, p);
  if (std::abs(det) < 1e-20F) return -1.0F;
  const float inv = 1.0F / det;
  const QVector3D s = o - a;
  const float u = QVector3D::dotProduct(s, p) * inv;
  if (u < 0.0F || u > 1.0F) return -1.0F;
  const QVector3D q = QVector3D::crossProduct(s, e1);
  const float v = QVector3D::dotProduct(d, q) * inv;
  if (v < 0.0F || u + v > 1.0F) return -1.0F;
  return QVector3D::dotProduct(e2, q) * inv;
}

/// Distance of p to segment ab in 2D and the segment parameter of the closest point.
std::pair<double, double> point_segment_2d(const QPointF& p, const QPointF& a, const QPointF& b) {
  const QPointF ab = b - a;
  const double len2 = QPointF::dotProduct(ab, ab);
  const double s = len2 > 0.0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
  const QPointF c = a + ab * s;
  return {std::hypot(p.x() - c.x(), p.y() - c.y()), s};
}

float farthest_wpp(const viewport::Camera& cam, const Box3& b) {
  float w = 0.0F;
  for (int i = 0; i < 8; ++i) {
    const QVector3D c((i & 1) != 0 ? b.max.x() : b.min.x(), (i & 2) != 0 ? b.max.y() : b.min.y(),
                      (i & 4) != 0 ? b.max.z() : b.min.z());
    w = std::max(w, cam.worldPerPixelAt(c));
  }
  return w;
}

bool better(const Hit& a, const Hit& b) {  // a better than b (same kind)
  if (std::abs(a.distance_px - b.distance_px) > 0.5F) return a.distance_px < b.distance_px;
  return a.depth < b.depth;
}

// ---- rectangle tests
bool segment_hits_rect(QPointF a, QPointF b, const QRectF& r) {  // Liang–Barsky
  double t0 = 0.0;
  double t1 = 1.0;
  const double dx = b.x() - a.x();
  const double dy = b.y() - a.y();
  const std::array<double, 4> p = {-dx, dx, -dy, dy};
  const std::array<double, 4> q = {a.x() - r.left(), r.right() - a.x(), a.y() - r.top(),
                                   r.bottom() - a.y()};
  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0.0) {
      if (q[i] < 0.0) return false;
    } else {
      const double t = q[i] / p[i];
      if (p[i] < 0.0) {
        t0 = std::max(t0, t);
      } else {
        t1 = std::min(t1, t);
      }
      if (t0 > t1) return false;
    }
  }
  return true;
}

bool point_in_triangle(const QPointF& p, const QPointF& a, const QPointF& b, const QPointF& c) {
  auto cross = [](const QPointF& o, const QPointF& u, const QPointF& v) {
    return (u.x() - o.x()) * (v.y() - o.y()) - (u.y() - o.y()) * (v.x() - o.x());
  };
  const double d1 = cross(a, b, p);
  const double d2 = cross(b, c, p);
  const double d3 = cross(c, a, p);
  const bool neg = d1 < 0 || d2 < 0 || d3 < 0;
  const bool pos = d1 > 0 || d2 > 0 || d3 > 0;
  return !(neg && pos);
}

}  // namespace

std::optional<Hit> pick_face(const std::vector<Target>& targets, const viewport::Camera& camera,
                             const QPointF& cursor_px, bool sub_elements) {
  const viewport::Ray ray = camera.pickRay(cursor_px);
  const QVector3D& o = ray.origin;
  const QVector3D& d = ray.direction;
  std::optional<Hit> face;
  float best_t = kInf;
  for (const Target& tg : targets) {
    const DisplayMesh& m = *tg.mesh;
    if (m.triangle_bvh.empty()) continue;
    std::optional<std::size_t> hit_tri;
    float hit_t = kInf;
    m.triangle_bvh.raycast(o, d, 0.0F, best_t, [&](std::uint32_t t, float& t_max) {
      const float h = ray_triangle(o, d, m.vertex(m.indices[3 * t]), m.vertex(m.indices[3 * t + 1]),
                                   m.vertex(m.indices[3 * t + 2]));
      if (h > 0.0F && h < t_max) {
        t_max = h;
        hit_t = h;
        hit_tri = t;
      }
    });
    if (hit_tri && hit_t < best_t) {
      best_t = hit_t;
      Hit hit;
      hit.kind = Hit::Kind::Face;
      hit.item.entity = tg.id;
      if (sub_elements && m.faceCount() > 0) {
        hit.item.sub = {SubElement::Kind::Face, m.faceOfTriangle(*hit_tri)};
      }
      hit.point = o + d * hit_t;
      hit.depth = hit_t;
      face = hit;
    }
  }
  return face;
}

float farthest_world_per_pixel(const viewport::Camera& camera, const Box3& box) {
  return farthest_wpp(camera, box);
}

std::optional<Hit> pick(const std::vector<Target>& targets, const viewport::Camera& camera,
                        const Query& query) {
  const viewport::Ray ray = camera.pickRay(query.cursor_px);
  const QVector3D& o = ray.origin;
  const QVector3D& d = ray.direction;

  // ---- faces: closest ray hit
  std::optional<Hit> face;
  if (query.faces) face = pick_face(targets, camera, query.cursor_px, query.sub_elements);
  const float occlusion_tol =
      face ? 2.0F * query.radius_px * camera.worldPerPixelAt(face->point) : 0.0F;
  auto occluded = [&](float depth) { return face && depth > face->depth + occlusion_tol; };

  std::optional<Hit> point_hit;
  std::optional<Hit> curve_hit;
  auto consider = [&](std::optional<Hit>& slot, const Hit& h) {
    if (occluded(h.depth)) return;
    if (!slot || better(h, *slot)) slot = h;
  };

  for (const Target& tg : targets) {
    const DisplayMesh& m = *tg.mesh;
    // ---- points and B-rep vertices
    auto test_points = [&](const std::vector<float>& xyz, bool vertices) {
      for (std::size_t i = 0; i + 2 < xyz.size(); i += 3) {
        const QVector3D p(xyz[i], xyz[i + 1], xyz[i + 2]);
        const auto s = camera.project(p);
        if (!s) continue;
        const float dist = static_cast<float>(std::hypot(s->x() - query.cursor_px.x(),
                                                         s->y() - query.cursor_px.y()));
        if (dist > query.radius_px) continue;
        Hit h;
        h.kind = Hit::Kind::Point;
        h.item.entity = tg.id;
        if (vertices) h.item.sub = {SubElement::Kind::Vertex, static_cast<int>(i / 3)};
        h.point = p;
        h.depth = QVector3D::dotProduct(p - o, d);
        h.distance_px = dist;
        consider(point_hit, h);
      }
    };
    test_points(m.points, false);
    if (query.sub_elements) test_points(m.corners, true);

    // ---- curves, edges, iso-curves
    if (m.segment_bvh.empty()) continue;
    const float inflate = query.radius_px * farthest_wpp(camera, m.bounds);
    m.segment_bvh.raycast(o, d, inflate, kInf, [&](std::uint32_t si, float& /*t_max*/) {
      const DisplayMesh::Segment& seg = m.segments[si];
      if (seg.iso && !query.isocurves) return;
      const auto [a, b] = m.segmentPoints(seg);
      const auto sa = camera.project(a);
      const auto sb = camera.project(b);
      if (!sa || !sb) return;
      const auto [dist, s] = point_segment_2d(query.cursor_px, *sa, *sb);
      if (dist > query.radius_px) return;
      Hit h;
      h.kind = Hit::Kind::Curve;
      h.item.entity = tg.id;
      if (query.sub_elements && m.faceCount() > 0) {
        h.item.sub = seg.iso ? SubElement{SubElement::Kind::Face, m.iso_faces[seg.polyline]}
                             : SubElement{SubElement::Kind::Edge, static_cast<int>(seg.polyline)};
      }
      h.point = a + (b - a) * static_cast<float>(s);
      h.depth = QVector3D::dotProduct(h.point - o, d);
      h.distance_px = static_cast<float>(dist);
      consider(curve_hit, h);
    });
  }
  if (point_hit) return point_hit;
  if (curve_hit) return curve_hit;
  return face;
}

std::vector<document::EntityId> pick_rect(const std::vector<Target>& targets,
                                          const viewport::Camera& camera, const QRectF& rect_in,
                                          bool crossing) {
  const QRectF rect = rect_in.normalized();
  auto inside = [&](const QPointF& p) {
    return p.x() >= rect.left() && p.x() <= rect.right() && p.y() >= rect.top() &&
           p.y() <= rect.bottom();
  };
  std::vector<document::EntityId> out;
  for (const Target& tg : targets) {
    const DisplayMesh& m = *tg.mesh;
    if (!m.bounds.valid()) continue;

    // Quick tests on the projected bounding box.
    bool all_corners = true;
    double x0 = 1e300;
    double y0 = 1e300;
    double x1 = -1e300;
    double y1 = -1e300;
    bool first = true;
    for (int i = 0; i < 8; ++i) {
      const QVector3D c((i & 1) != 0 ? m.bounds.max.x() : m.bounds.min.x(),
                        (i & 2) != 0 ? m.bounds.max.y() : m.bounds.min.y(),
                        (i & 4) != 0 ? m.bounds.max.z() : m.bounds.min.z());
      const auto s = camera.project(c);
      if (!s) {
        all_corners = false;
        continue;
      }
      // (QRectF::united ignores empty rectangles, so the box is accumulated by hand)
      x0 = std::min(x0, s->x());
      y0 = std::min(y0, s->y());
      x1 = std::max(x1, s->x());
      y1 = std::max(y1, s->y());
      first = false;
      if (!inside(*s)) all_corners = false;
    }
    if (all_corners) {  // the projected box lies inside: selected in both modes
      out.push_back(tg.id);
      continue;
    }
    if (!first && (x1 + 1 < rect.left() || x0 - 1 > rect.right() || y1 + 1 < rect.top() ||
                   y0 - 1 > rect.bottom())) {
      continue;
    }

    // Detailed test on the display geometry.
    bool any_inside = false;
    bool all_inside = true;
    auto visit = [&](const QVector3D& p) {
      const auto s = camera.project(p);
      const bool in = s && inside(*s);
      any_inside = any_inside || in;
      all_inside = all_inside && in;
    };
    for (std::uint32_t i = 0; i < m.vertexCount() && (crossing ? !any_inside : all_inside); ++i) {
      visit(m.vertex(i));
    }
    for (const auto& pl : m.edges) {
      for (std::size_t i = 0; i + 2 < pl.size(); i += 3) visit({pl[i], pl[i + 1], pl[i + 2]});
    }
    for (std::size_t i = 0; i + 2 < m.points.size(); i += 3) {
      visit({m.points[i], m.points[i + 1], m.points[i + 2]});
    }
    if (!crossing) {
      if (all_inside) out.push_back(tg.id);
      continue;
    }
    if (any_inside) {
      out.push_back(tg.id);
      continue;
    }
    // crossing: segments through the rectangle, or the rectangle inside a triangle
    bool hit = false;
    for (const auto& pl : m.edges) {
      for (std::size_t i = 0; !hit && i + 5 < pl.size(); i += 3) {
        const auto a = camera.project({pl[i], pl[i + 1], pl[i + 2]});
        const auto b = camera.project({pl[i + 3], pl[i + 4], pl[i + 5]});
        hit = a && b && segment_hits_rect(*a, *b, rect);
      }
      if (hit) break;
    }
    const QPointF centre = rect.center();
    for (std::size_t t = 0; !hit && t < m.triangleCount(); ++t) {
      const auto a = camera.project(m.vertex(m.indices[3 * t]));
      const auto b = camera.project(m.vertex(m.indices[3 * t + 1]));
      const auto c = camera.project(m.vertex(m.indices[3 * t + 2]));
      if (!a || !b || !c) continue;
      hit = point_in_triangle(centre, *a, *b, *c) || segment_hits_rect(*a, *b, rect) ||
            segment_hits_rect(*b, *c, rect) || segment_hits_rect(*c, *a, rect);
    }
    if (hit) out.push_back(tg.id);
  }
  return out;
}

}  // namespace nfcad::pick
