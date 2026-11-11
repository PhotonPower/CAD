#include "tessellation/tessellator.hpp"

#include <CDT.h>
#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <numeric>

namespace nfcad::tessellation {

namespace {

using nf::Vec3;

constexpr int kMaxDepth = 10;
constexpr std::size_t kMaxLinesPerDirection = 400;
constexpr std::size_t kParallelFaceThreshold = 8;

double deg2rad(double d) { return d * std::numbers::pi / 180.0; }

double dist_point_segment(const Vec3& p, const Vec3& a, const Vec3& b) {
  const Vec3 ab = b - a;
  const double len2 = nf::dot(ab, ab);
  if (len2 <= 0.0) return nf::norm(p - a);
  const double t = std::clamp(nf::dot(p - a, ab) / len2, 0.0, 1.0);
  return nf::norm(p - (a + ab * t));
}

double angle_between(const Vec3& a, const Vec3& b) {
  const double na = nf::norm(a);
  const double nb = nf::norm(b);
  if (na <= 0.0 || nb <= 0.0) return 0.0;
  return std::acos(std::clamp(nf::dot(a, b) / (na * nb), -1.0, 1.0));
}

void add_point(Polyline& pl, const Vec3& p) {
  pl.push_back(static_cast<float>(p.x));
  pl.push_back(static_cast<float>(p.y));
  pl.push_back(static_cast<float>(p.z));
}

/// Distinct knot values inside the domain [knots[p], knots[n]].
std::vector<double> span_breaks(const nf::KnotVector& knots, std::size_t degree,
                                std::size_t n_ctrl) {
  std::vector<double> out;
  const double a = knots[degree];
  const double b = knots[n_ctrl];
  for (std::size_t i = degree; i <= n_ctrl; ++i) {
    const double k = std::clamp(knots[i], a, b);
    if (out.empty() || k > out.back()) out.push_back(k);
  }
  return out;
}

/// Adaptive refinement of a parametric map t -> 3D on [a, b] (end point b appended).
/// `extra_split(a, b)` may force splitting (e.g. parameter-space length limits).
template <class F, class G>
void refine(const F& f, const G& extra_split, double a, double b, const Vec3& pa, const Vec3& pb,
            const TessParams& params, int depth, std::vector<double>& ts) {
  const double m = 0.5 * (a + b);
  const Vec3 pm = f(m);
  bool split = depth < kMaxDepth && extra_split(a, b);
  if (!split && depth < kMaxDepth) {
    const double dev = dist_point_segment(pm, pa, pb);
    const double len = nf::norm(pb - pa);
    split = dev > params.chord ||
            (len > 0.1 * params.chord && angle_between(pm - pa, pb - pm) > deg2rad(params.angle_deg));
  }
  if (split) {
    refine(f, extra_split, a, m, pa, pm, params, depth + 1, ts);
    refine(f, extra_split, m, b, pm, pb, params, depth + 1, ts);
  } else {
    ts.push_back(b);
  }
}

/// Parameters of an adaptive sampling of t -> f(t) over the given span breaks.
template <class F, class G>
std::vector<double> adaptive_params(const F& f, const G& extra_split,
                                    const std::vector<double>& breaks, std::size_t degree,
                                    const TessParams& params) {
  std::vector<double> ts;
  if (breaks.size() < 2) return ts;
  ts.push_back(breaks.front());
  const int initial = degree <= 1 ? 1 : 4;
  for (std::size_t s = 0; s + 1 < breaks.size(); ++s) {
    const double a = breaks[s];
    const double b = breaks[s + 1];
    for (int k = 0; k < initial; ++k) {
      const double t0 = a + (b - a) * k / initial;
      const double t1 = a + (b - a) * (k + 1) / initial;
      refine(f, extra_split, t0, t1, f(t0), f(t1), params, 0, ts);
    }
  }
  return ts;
}

/// Parameter values of the display grid in one direction of a surface: per knot span as many
/// segments as chord height and turning angle require (estimated from second derivatives).
std::vector<double> grid_lines(const nf::NurbsSurface& s, bool along_u, const TessParams& params) {
  const std::size_t degree = along_u ? s.degree_u() : s.degree_v();
  const auto breaks = along_u ? span_breaks(s.knots_u(), s.degree_u(), s.num_u())
                              : span_breaks(s.knots_v(), s.degree_v(), s.num_v());
  const auto other = along_u ? s.domain_v() : s.domain_u();
  std::vector<std::size_t> counts;
  for (std::size_t k = 0; k + 1 < breaks.size(); ++k) {
    const double a = breaks[k];
    const double b = breaks[k + 1];
    if (degree <= 1) {
      counts.push_back(1);
      continue;
    }
    double max_second = 0.0;
    double max_turn_rate = 0.0;  // |S' x S''| / |S'|^2: turning angle per unit parameter
    for (int i = 0; i <= 3; ++i) {
      const double t = a + (b - a) * i / 3.0;
      for (int j = 0; j <= 4; ++j) {
        const double o = other[0] + (other[1] - other[0]) * j / 4.0;
        const auto d = along_u ? s.derivatives(t, o, 2) : s.derivatives(o, t, 2);
        const Vec3& d1 = along_u ? d[1][0] : d[0][1];
        const Vec3& d2 = along_u ? d[2][0] : d[0][2];
        max_second = std::max(max_second, nf::norm(d2));
        const double n1 = nf::dot(d1, d1);
        if (n1 > 1e-24) max_turn_rate = std::max(max_turn_rate, nf::norm(nf::cross(d1, d2)) / n1);
      }
    }
    const double h = b - a;
    const double n_chord = h * std::sqrt(max_second / (8.0 * params.chord));
    const double n_angle = h * max_turn_rate / deg2rad(params.angle_deg);
    const double n = std::max({2.0, std::ceil(n_chord), std::ceil(n_angle)});
    counts.push_back(static_cast<std::size_t>(
        std::min(n, static_cast<double>(params.max_segments_per_span))));
  }
  // Limit the total; scale all spans down evenly.
  const std::size_t total = std::accumulate(counts.begin(), counts.end(), std::size_t{0});
  if (total > kMaxLinesPerDirection) {
    for (std::size_t& c : counts) {
      c = std::max<std::size_t>(1, c * kMaxLinesPerDirection / total);
    }
  }
  std::vector<double> out;
  if (breaks.empty()) return out;
  out.push_back(breaks.front());
  for (std::size_t k = 0; k + 1 < breaks.size(); ++k) {
    for (std::size_t i = 1; i <= counts[k]; ++i) {
      out.push_back(breaks[k] + (breaks[k + 1] - breaks[k]) * static_cast<double>(i) /
                                    static_cast<double>(counts[k]));
    }
  }
  return out;
}

Vec3 safe_normal(const nf::NurbsSurface& s, double u, double v) {
  try {
    return s.normal(u, v);
  } catch (...) {
    return {0.0, 0.0, 1.0};
  }
}

/// Appends a vertex; (u, v) are the surface parameters, stored normalised to [0, 1].
void push_vertex(DisplayMesh& m, const nf::NurbsSurface& s, double u, double v) {
  const Vec3 p = s.evaluate(u, v);
  const Vec3 n = safe_normal(s, u, v);
  const auto du = s.domain_u();
  const auto dv = s.domain_v();
  const double nu = du[1] > du[0] ? (u - du[0]) / (du[1] - du[0]) : 0.0;
  const double nv = dv[1] > dv[0] ? (v - dv[0]) / (dv[1] - dv[0]) : 0.0;
  m.vertices.insert(m.vertices.end(),
                    {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z),
                     static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z),
                     static_cast<float>(nu), static_cast<float>(nv)});
}

struct Uv {
  double u, v;
};

/// Sorted crossings of the boundary segments with the line coordinate[axis] = value
/// (axis 0: u = value, returns v values; axis 1: v = value, returns u values).
std::vector<double> crossings(const std::vector<std::vector<Uv>>& loops, int axis, double value) {
  std::vector<double> out;
  for (const auto& loop : loops) {
    for (std::size_t i = 0; i < loop.size(); ++i) {
      const Uv& a = loop[i];
      const Uv& b = loop[(i + 1) % loop.size()];
      const double ca = axis == 0 ? a.u : a.v;
      const double cb = axis == 0 ? b.u : b.v;
      if ((ca <= value) == (cb <= value)) continue;  // half-open rule
      const double t = (value - ca) / (cb - ca);
      const double oa = axis == 0 ? a.v : a.u;
      const double ob = axis == 0 ? b.v : b.u;
      out.push_back(oa + (ob - oa) * t);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

/// Iso-curves of a face, clipped by the trim loops (even-odd inside test in parameter space).
void add_isocurves(const nf::NurbsSurface& s, const std::vector<std::vector<Uv>>* loops,
                   const std::vector<double>& us, const std::vector<double>& vs,
                   const TessParams& params, DisplayMesh& out) {
  const int n = params.iso_per_direction;
  if (n <= 0) return;
  for (int axis = 0; axis < 2; ++axis) {
    const auto dom = axis == 0 ? s.domain_u() : s.domain_v();
    const auto& along = axis == 0 ? vs : us;  // sample positions along the iso-curve
    for (int k = 1; k <= n; ++k) {
      const double c = dom[0] + (dom[1] - dom[0]) * k / (n + 1);
      std::vector<double> bounds;
      if (loops == nullptr) {
        bounds = {along.front(), along.back()};
      } else {
        bounds = crossings(*loops, axis, c);
      }
      for (std::size_t i = 0; i + 1 < bounds.size(); i += 2) {
        const double a = bounds[i];
        const double b = bounds[i + 1];
        Polyline pl;
        auto eval = [&](double t) { return axis == 0 ? s.evaluate(c, t) : s.evaluate(t, c); };
        add_point(pl, eval(a));
        for (double t : along) {
          if (t > a && t < b) add_point(pl, eval(t));
        }
        add_point(pl, eval(b));
        out.isocurves.push_back(std::move(pl));
      }
    }
  }
}

void tessellate_untrimmed(const nf::NurbsSurface& s, const std::vector<double>& us,
                          const std::vector<double>& vs, DisplayMesh& out, bool boundary) {
  const auto base = static_cast<std::uint32_t>(out.vertexCount());
  const std::size_t nv = vs.size();
  for (double u : us) {
    for (double v : vs) push_vertex(out, s, u, v);
  }
  for (std::size_t i = 0; i + 1 < us.size(); ++i) {
    for (std::size_t j = 0; j + 1 < nv; ++j) {
      const auto a = static_cast<std::uint32_t>(base + i * nv + j);
      const auto b = static_cast<std::uint32_t>(a + nv);
      out.indices.insert(out.indices.end(), {a, b, b + 1, a, b + 1, a + 1});
    }
  }
  if (boundary) {
    Polyline pl;
    for (double v : vs) add_point(pl, s.evaluate(us.front(), v));
    for (std::size_t i = 1; i < us.size(); ++i) add_point(pl, s.evaluate(us[i], vs.back()));
    for (std::size_t j = nv - 1; j-- > 0;) add_point(pl, s.evaluate(us.back(), vs[j]));
    for (std::size_t i = us.size() - 1; i-- > 0;) add_point(pl, s.evaluate(us[i], vs.front()));
    out.edges.push_back(std::move(pl));
  }
}

void tessellate_trimmed(const nf::TrimmedSurface& face, const std::vector<double>& us,
                        const std::vector<double>& vs, const TessParams& params,
                        DisplayMesh& out, bool boundary) {
  const nf::NurbsSurface& s = face.surface();

  // Scale (u, v) so that parameter distances approximate 3D distances: well-shaped triangles.
  double su = 0.0;
  double sv = 0.0;
  int count = 0;
  for (int i = 0; i <= 4; ++i) {
    for (int j = 0; j <= 4; ++j) {
      const double u = us.front() + (us.back() - us.front()) * i / 4.0;
      const double v = vs.front() + (vs.back() - vs.front()) * j / 4.0;
      const auto d = s.first_derivatives(u, v);
      su += nf::norm(d[1]);
      sv += nf::norm(d[2]);
      ++count;
    }
  }
  su = su > 0.0 ? su / count : 1.0;
  sv = sv > 0.0 ? sv / count : 1.0;
  // Longest boundary segment in scaled parameter space: about the average grid cell.
  const double cell = std::max((us.back() - us.front()) * su / static_cast<double>(us.size() - 1),
                               (vs.back() - vs.front()) * sv / static_cast<double>(vs.size() - 1));

  // ---- boundary loops, adaptive in 3D and limited in length
  std::vector<std::vector<Uv>> loops;
  auto sample_loop = [&](const nf::TrimLoop& loop) {
    std::vector<Uv> pts;
    for (const nf::NurbsCurve& c : loop.curves) {
      auto uv = [&](double t) {
        const Vec3 q = c.evaluate(t);
        return Uv{q.x, q.y};
      };
      auto f = [&](double t) {
        const Uv q = uv(t);
        return s.evaluate(q.u, q.v);
      };
      auto too_long = [&](double a, double b) {
        const Uv qa = uv(a);
        const Uv qb = uv(b);
        return std::hypot((qb.u - qa.u) * su, (qb.v - qa.v) * sv) > cell;
      };
      const auto ts = adaptive_params(f, too_long,
                                      span_breaks(c.knots(), c.degree(), c.num_control_points()),
                                      c.degree(), params);
      for (std::size_t i = 0; i + 1 < ts.size(); ++i) {  // last point = start of next curve
        const Uv q = uv(ts[i]);
        if (pts.empty() || std::hypot(q.u - pts.back().u, q.v - pts.back().v) > 1e-12) {
          pts.push_back(q);
        }
      }
    }
    if (pts.size() >= 3) loops.push_back(std::move(pts));
  };
  sample_loop(face.outer());
  for (const nf::TrimLoop& h : face.holes()) sample_loop(h);
  if (loops.empty()) return;

  if (boundary) {
    for (const auto& loop : loops) {
      Polyline pl;
      for (const Uv& q : loop) add_point(pl, s.evaluate(q.u, q.v));
      add_point(pl, s.evaluate(loop.front().u, loop.front().v));
      out.edges.push_back(std::move(pl));
    }
  }

  // ---- CDT input: boundary points + constraint edges
  std::vector<CDT::V2d<double>> verts;
  std::vector<CDT::Edge> edges;
  for (const auto& loop : loops) {
    const auto first = static_cast<CDT::VertInd>(verts.size());
    for (std::size_t i = 0; i < loop.size(); ++i) {
      verts.emplace_back(loop[i].u * su, loop[i].v * sv);
      const auto a = static_cast<CDT::VertInd>(first + i);
      const auto b = static_cast<CDT::VertInd>(first + (i + 1) % loop.size());
      edges.emplace_back(a, b);
    }
  }

  // ---- interior grid points: inside (even-odd per grid row) and not too close to the boundary
  // Buckets of boundary segments over the grid cells for the distance test.
  const std::size_t cu = us.size() - 1;
  const std::size_t cv = vs.size() - 1;
  auto cell_index = [](const std::vector<double>& lines, double x) {
    const auto it = std::upper_bound(lines.begin(), lines.end(), x);
    const auto i = static_cast<std::ptrdiff_t>(it - lines.begin()) - 1;
    return static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
        i, 0, static_cast<std::ptrdiff_t>(lines.size()) - 2));
  };
  std::vector<std::vector<std::pair<Uv, Uv>>> buckets(cu * cv);
  for (const auto& loop : loops) {
    for (std::size_t i = 0; i < loop.size(); ++i) {
      const Uv& a = loop[i];
      const Uv& b = loop[(i + 1) % loop.size()];
      const std::size_t i0 = cell_index(us, std::min(a.u, b.u));
      const std::size_t i1 = cell_index(us, std::max(a.u, b.u));
      const std::size_t j0 = cell_index(vs, std::min(a.v, b.v));
      const std::size_t j1 = cell_index(vs, std::max(a.v, b.v));
      for (std::size_t ci = i0; ci <= i1; ++ci) {
        for (std::size_t cj = j0; cj <= j1; ++cj) buckets[ci * cv + cj].emplace_back(a, b);
      }
    }
  }
  auto near_boundary = [&](std::size_t i, std::size_t j, double min_dist) {
    for (std::size_t ci = (i > 0 ? i - 1 : 0); ci <= std::min(i, cu - 1); ++ci) {
      for (std::size_t cj = (j > 0 ? j - 1 : 0); cj <= std::min(j, cv - 1); ++cj) {
        for (const auto& [a, b] : buckets[ci * cv + cj]) {
          const Vec3 p(us[i] * su, vs[j] * sv, 0.0);
          if (dist_point_segment(p, Vec3(a.u * su, a.v * sv, 0.0), Vec3(b.u * su, b.v * sv, 0.0)) <
              min_dist) {
            return true;
          }
        }
      }
    }
    return false;
  };
  for (std::size_t j = 0; j < vs.size(); ++j) {
    const auto xs = crossings(loops, 1, vs[j]);
    std::size_t k = 0;  // index into xs
    for (std::size_t i = 0; i < us.size(); ++i) {
      while (k < xs.size() && xs[k] <= us[i]) ++k;
      if (k % 2 == 0) continue;  // outside
      const double du = std::min(i > 0 ? us[i] - us[i - 1] : 1e300,
                                 i + 1 < us.size() ? us[i + 1] - us[i] : 1e300) * su;
      const double dv = std::min(j > 0 ? vs[j] - vs[j - 1] : 1e300,
                                 j + 1 < vs.size() ? vs[j + 1] - vs[j] : 1e300) * sv;
      if (near_boundary(i, j, 0.35 * std::min(du, dv))) continue;
      verts.emplace_back(us[i] * su, vs[j] * sv);
    }
  }

  // ---- triangulate
  try {
    CDT::RemoveDuplicatesAndRemapEdges(verts, edges);
    CDT::Triangulation<double> cdt(CDT::VertexInsertionOrder::Auto,
                                   CDT::IntersectingConstraintEdges::TryResolve, 0.0);
    cdt.insertVertices(verts);
    cdt.insertEdges(edges);
    cdt.eraseOuterTrianglesAndHoles();
    const auto base = static_cast<std::uint32_t>(out.vertexCount());
    for (const auto& p : cdt.vertices) {
      const double u = p.x / su;
      const double v = p.y / sv;
      push_vertex(out, s, u, v);
    }
    for (const auto& t : cdt.triangles) {
      out.indices.insert(out.indices.end(), {base + static_cast<std::uint32_t>(t.vertices[0]),
                                             base + static_cast<std::uint32_t>(t.vertices[1]),
                                             base + static_cast<std::uint32_t>(t.vertices[2])});
    }
  } catch (const std::exception&) {
    // Degenerate trim loops: the face is shown by its edges only.
  }

  add_isocurves(s, &loops, us, vs, params, out);
}

/// Appends the mesh of one face (face index = number of faces so far).
void append_face(DisplayMesh& dst, DisplayMesh&& src) {
  if (dst.face_starts.empty()) dst.face_starts.push_back(0);
  const auto face = static_cast<std::int32_t>(dst.face_starts.size() - 1);
  dst.iso_faces.insert(dst.iso_faces.end(), src.isocurves.size(), face);
  const auto base = static_cast<std::uint32_t>(dst.vertexCount());
  dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());
  for (std::uint32_t i : src.indices) dst.indices.push_back(base + i);
  std::move(src.edges.begin(), src.edges.end(), std::back_inserter(dst.edges));
  std::move(src.isocurves.begin(), src.isocurves.end(), std::back_inserter(dst.isocurves));
  dst.points.insert(dst.points.end(), src.points.begin(), src.points.end());
  dst.face_starts.push_back(static_cast<std::uint32_t>(dst.indices.size()));
}

void single_face(const nf::TrimmedSurface& face, const TessParams& params, DisplayMesh& out) {
  DisplayMesh m;
  tessellate_face(face, params, m, true);
  append_face(out, std::move(m));
}

void compute_bounds(DisplayMesh& m) {
  m.bounds = {};
  for (std::size_t i = 0; i + kVertexStride <= m.vertices.size(); i += kVertexStride) {
    m.bounds.add({m.vertices[i], m.vertices[i + 1], m.vertices[i + 2]});
  }
  auto add_flat = [&](const std::vector<float>& xyz) {
    for (std::size_t i = 0; i + 2 < xyz.size(); i += 3) m.bounds.add({xyz[i], xyz[i + 1], xyz[i + 2]});
  };
  for (const Polyline& pl : m.edges) add_flat(pl);
  for (const Polyline& pl : m.isocurves) add_flat(pl);
  add_flat(m.points);
}

void add_control_points(const std::vector<Vec3>& pts, Box3& box) {
  for (const Vec3& p : pts) {
    box.add(QVector3D(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)));
  }
}

}  // namespace

// ------------------------------------------------------------------------------- public

TessParams default_params(const document::Geometry& g) {
  Box3 box;
  std::visit(
      [&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, nf::Vec3>) {
          add_control_points({v}, box);
        } else if constexpr (std::is_same_v<T, nf::NurbsCurve> ||
                             std::is_same_v<T, nf::NurbsSurface>) {
          add_control_points(v.control_points(), box);
        } else if constexpr (std::is_same_v<T, nf::TrimmedSurface>) {
          add_control_points(v.surface().control_points(), box);
        } else {
          for (const auto& f : v.faces()) add_control_points(f.surface().control_points(), box);
        }
      },
      g);
  TessParams p;
  p.chord = std::max(static_cast<double>(box.radius()) * 2e-3, 1e-6);
  return p;
}

std::vector<Vec3> tessellate_curve(const nf::NurbsCurve& curve, const TessParams& params) {
  auto f = [&](double t) { return curve.evaluate(t); };
  auto never = [](double, double) { return false; };
  const auto ts =
      adaptive_params(f, never, span_breaks(curve.knots(), curve.degree(), curve.num_control_points()),
                      curve.degree(), params);
  std::vector<Vec3> out;
  out.reserve(ts.size());
  for (double t : ts) out.push_back(curve.evaluate(t));
  return out;
}

void tessellate_face(const nf::TrimmedSurface& face, const TessParams& params, DisplayMesh& out,
                     bool boundary) {
  const nf::NurbsSurface& s = face.surface();
  const auto us = grid_lines(s, true, params);
  const auto vs = grid_lines(s, false, params);
  if (us.size() < 2 || vs.size() < 2) return;
  if (face.is_untrimmed()) {
    tessellate_untrimmed(s, us, vs, out, boundary);
    add_isocurves(s, nullptr, us, vs, params, out);
  } else {
    tessellate_trimmed(face, us, vs, params, out, boundary);
  }
}

DisplayMesh tessellate(const document::Geometry& g, const TessParams& params) {
  DisplayMesh out;
  std::visit(
      [&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, nf::Vec3>) {
          add_point(out.points, v);
        } else if constexpr (std::is_same_v<T, nf::NurbsCurve>) {
          Polyline pl;
          for (const Vec3& p : tessellate_curve(v, params)) add_point(pl, p);
          out.edges.push_back(std::move(pl));
        } else if constexpr (std::is_same_v<T, nf::NurbsSurface>) {
          single_face(nf::TrimmedSurface(v), params, out);
        } else if constexpr (std::is_same_v<T, nf::TrimmedSurface>) {
          single_face(v, params, out);
        } else {
          // Shell: faces in parallel, shared B-rep edges once.
          const auto& faces = v.faces();
          if (faces.size() >= kParallelFaceThreshold) {
            std::vector<std::size_t> idx(faces.size());
            std::iota(idx.begin(), idx.end(), std::size_t{0});
            const QList<DisplayMesh> parts =
                QtConcurrent::blockingMapped<QList<DisplayMesh>>(idx, [&](std::size_t i) {
                  DisplayMesh m;
                  tessellate_face(faces[i], params, m, false);
                  return m;
                });
            for (DisplayMesh part : parts) append_face(out, std::move(part));
          } else {
            for (const auto& f : faces) {
              DisplayMesh m;
              tessellate_face(f, params, m, false);
              append_face(out, std::move(m));
            }
          }
          for (const Vec3& p : v.vertices()) add_point(out.corners, p);
          for (const nf::ShellEdge& e : v.edges()) {
            Polyline pl;
            for (const Vec3& p : tessellate_curve(e.curve, params)) add_point(pl, p);
            out.edges.push_back(std::move(pl));
          }
        }
      },
      g);
  compute_bounds(out);
  build_pick_index(out);
  return out;
}

}  // namespace nfcad::tessellation
