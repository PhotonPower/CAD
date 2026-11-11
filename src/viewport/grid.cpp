#include "viewport/grid.hpp"

#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdint>

namespace nfcad::viewport {

namespace {
constexpr int kMajorEvery = 10;
constexpr std::int64_t kMaxLinesPerDirection = 400;

void add_line(std::vector<render::LineVertex>& out, const QVector3D& a, const QVector3D& b,
              const float (&c)[4]) {
  out.push_back({a.x(), a.y(), a.z(), c[0], c[1], c[2], c[3]});
  out.push_back({b.x(), b.y(), b.z(), c[0], c[1], c[2], c[3]});
}
}  // namespace

void world_axis_color(const QVector3D& dir, float (&rgba)[4]) {
  static constexpr float kColors[3][4] = {{0.90F, 0.35F, 0.35F, 0.95F},
                                          {0.40F, 0.82F, 0.40F, 0.95F},
                                          {0.40F, 0.58F, 0.95F, 0.95F}};
  const float ax = std::abs(dir.x());
  const float ay = std::abs(dir.y());
  const float az = std::abs(dir.z());
  const int i = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
  std::copy(std::begin(kColors[i]), std::end(kColors[i]), std::begin(rgba));
}

float grid_spacing(float world_per_pixel, float min_px) {
  const float target = std::max(world_per_pixel * min_px, 1e-9F);
  return std::pow(10.0F, std::ceil(std::log10(target)));
}

std::vector<render::LineVertex> build_grid(const CPlane& plane, const QVector3D& center,
                                           float spacing, float half_extent,
                                           const GridColors& colors) {
  std::vector<render::LineVertex> out;
  if (!(spacing > 0.0F) || !(half_extent > 0.0F)) return out;

  // Centre in plane coordinates, snapped to the major spacing.
  const QVector3D rel = center - plane.origin;
  const double major = static_cast<double>(spacing) * kMajorEvery;
  const double cu = std::round(QVector3D::dotProduct(rel, plane.u) / major) * major;
  const double cv = std::round(QVector3D::dotProduct(rel, plane.v) / major) * major;

  std::int64_t n = static_cast<std::int64_t>(std::ceil(half_extent / spacing));
  n = std::clamp<std::int64_t>((n + kMajorEvery - 1) / kMajorEvery * kMajorEvery, kMajorEvery,
                               kMaxLinesPerDirection / 2 / kMajorEvery * kMajorEvery);
  const double half = static_cast<double>(n) * spacing;

  auto at = [&](double u, double v) {
    return plane.origin + plane.u * static_cast<float>(u) + plane.v * static_cast<float>(v);
  };

  out.reserve(static_cast<std::size_t>(4 * (2 * n + 1) + 4));
  // Index of the grid line counted from the plane origin decides minor / major / axis.
  const auto first_u = static_cast<std::int64_t>(std::llround(cu / spacing)) - n;
  const auto first_v = static_cast<std::int64_t>(std::llround(cv / spacing)) - n;
  bool has_u_axis = false;
  bool has_v_axis = false;
  for (std::int64_t i = 0; i <= 2 * n; ++i) {
    const std::int64_t ku = first_u + i;
    const std::int64_t kv = first_v + i;
    const double u = static_cast<double>(ku) * spacing;
    const double v = static_cast<double>(kv) * spacing;
    // line of constant u (parallel to v)
    if (ku == 0) {
      has_v_axis = true;
    } else {
      add_line(out, at(u, cv - half), at(u, cv + half),
               ku % kMajorEvery == 0 ? colors.major : colors.minor);
    }
    // line of constant v (parallel to u)
    if (kv == 0) {
      has_u_axis = true;
    } else {
      add_line(out, at(cu - half, v), at(cu + half, v),
               kv % kMajorEvery == 0 ? colors.major : colors.minor);
    }
  }
  // Axes last so that they are drawn on top.
  float axis_color[4];
  if (has_u_axis) {
    world_axis_color(plane.u, axis_color);
    add_line(out, at(cu - half, 0.0), at(cu + half, 0.0), axis_color);
  }
  if (has_v_axis) {
    world_axis_color(plane.v, axis_color);
    add_line(out, at(0.0, cv - half), at(0.0, cv + half), axis_color);
  }
  return out;
}

}  // namespace nfcad::viewport
