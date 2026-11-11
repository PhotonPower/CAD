#pragma once

#include <QVector3D>
#include <vector>

#include "render/line_vertex.hpp"

namespace nfcad::viewport {

/// Construction plane: origin and two orthonormal in-plane axes (normal = u × v).
struct CPlane {
  QVector3D origin;
  QVector3D u{1.0F, 0.0F, 0.0F};
  QVector3D v{0.0F, 1.0F, 0.0F};
  [[nodiscard]] QVector3D normal() const { return QVector3D::crossProduct(u, v); }
};

/// Minor grid spacing: the smallest power of ten that is at least `min_px` pixels on screen.
[[nodiscard]] float grid_spacing(float world_per_pixel, float min_px = 12.0F);

struct GridColors {
  float minor[4] = {0.55F, 0.60F, 0.66F, 0.26F};
  float major[4] = {0.62F, 0.67F, 0.74F, 0.50F};
};

/// RGBA colour of the world axis closest to `dir`: X red, Y green, Z blue.
void world_axis_color(const QVector3D& dir, float (&rgba)[4]);

/// Line list (2 vertices per line) of a square grid in `plane` around the point of the plane
/// closest to `center`, covering at least `half_extent` in each direction. Every 10th line is a
/// major line; the two lines through the plane origin are drawn in the colours of the world
/// axes they follow (world_axis_color).
/// The grid is anchored at multiples of the major spacing so that it does not swim when panning.
[[nodiscard]] std::vector<render::LineVertex> build_grid(const CPlane& plane,
                                                         const QVector3D& center, float spacing,
                                                         float half_extent,
                                                         const GridColors& colors = {});

}  // namespace nfcad::viewport
