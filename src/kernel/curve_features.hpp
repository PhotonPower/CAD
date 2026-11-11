#pragma once

#include <optional>
#include <vector>

#include "document/entity.hpp"

namespace nfcad::kernel {

/// Exact circle or arc description of a curve (detected from the geometry, not from a tag).
struct CircleInfo {
  nf::Vec3 centre;
  nf::Vec3 normal;  ///< unit
  double radius = 0.0;
  bool full = false;  ///< closed circle (else an arc)
};

/// Circle / arc test: all samples equidistant from the circle through three of them and in its
/// plane (relative tolerance 1e-7).
[[nodiscard]] std::optional<CircleInfo> as_circle(const nf::NurbsCurve& c);

/// Characteristic points of a curve for the object snaps (docs/snapping.md).
struct CurveFeatures {
  bool closed = false;
  std::optional<nf::Vec3> start, end;  ///< open curves only
  std::optional<nf::Vec3> mid;         ///< open curves: point at half the arc length
  std::optional<nf::Vec3> centre;      ///< circles / arcs: centre; closed planar curves: centroid
  /// Circles / arcs: points where the planes through the centre parallel to the coordinate
  /// planes cut the circle (only those on the arc).
  std::vector<nf::Vec3> quadrants;
};

[[nodiscard]] CurveFeatures curve_features(const nf::NurbsCurve& c);

/// Exact foot point on the curve nearest to p.
[[nodiscard]] nf::Vec3 closest_on_curve(const nf::NurbsCurve& c, const nf::Vec3& p);

/// Exact intersection points of two curves (empty if none or on failure).
[[nodiscard]] std::vector<nf::Vec3> curve_intersections(const nf::NurbsCurve& a,
                                                        const nf::NurbsCurve& b);

/// Exact 3D curves behind the edge polylines of an entity's display mesh, in the same order:
/// shells: one curve per B-rep edge; curves: the curve; faces: the curves of each boundary loop.
[[nodiscard]] std::vector<std::vector<nf::NurbsCurve>> edge_curves(const document::Geometry& g);

/// B-rep vertices / loop corners of faces and shells (FaceCorner snaps).
[[nodiscard]] std::vector<nf::Vec3> corners(const document::Geometry& g);

}  // namespace nfcad::kernel
