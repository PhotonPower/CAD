#pragma once

// Helpers shared by the command implementations.

#include <functional>
#include <nurbforge/core/vec.hpp>
#include <nurbforge/nurbs/curve.hpp>

#include "commands/command.hpp"

namespace nfcad::commands::util {

inline constexpr double kEps = 1e-12;

inline nf::Vec3 V(const Point3& p) { return {p.x, p.y, p.z}; }
inline Point3 P(const nf::Vec3& v) { return {v.x, v.y, v.z}; }
inline double dist(const Point3& a, const Point3& b) { return nf::distance(V(a), V(b)); }

/// Component of p - origin in the plane (normal part removed).
inline nf::Vec3 in_plane(const Point3& p, const Point3& origin, const PlaneFrame& f) {
  const nf::Vec3 d = V(p) - V(origin);
  return d - V(f.n) * nf::dot(d, V(f.n));
}

/// Signed distance of p from origin along the plane normal.
inline double height(const Point3& p, const Point3& origin, const PlaneFrame& f) {
  return nf::dot(V(p) - V(origin), V(f.n));
}

inline Polyline3 sample(const nf::NurbsCurve& c, std::size_t n = 96) {
  Polyline3 out;
  for (const nf::Vec3& p : c.sample(n)) out.push_back(P(p));
  return out;
}

/// Circle polyline (preview).
Polyline3 circle(const nf::Vec3& centre, const nf::Vec3& normal, double r, std::size_t n = 72);

}  // namespace nfcad::commands::util
