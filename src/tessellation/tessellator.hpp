#pragma once

#include <vector>

#include "document/entity.hpp"
#include "tessellation/display_mesh.hpp"

namespace nfcad::tessellation {

/// Tolerances of the display tessellation (ADR 0004).
struct TessParams {
  double chord = 0.01;       ///< max. distance between mesh/polyline and exact geometry
  double angle_deg = 12.0;   ///< max. turning angle per segment
  int iso_per_direction = 3; ///< iso-curves per face and parameter direction (wireframe)
  int max_segments_per_span = 64;
};

/// Default parameters for a geometry: chord = 1e-3 of the control-point box diagonal.
[[nodiscard]] TessParams default_params(const document::Geometry& g);

/// Adaptive polyline of a curve (end points included).
[[nodiscard]] std::vector<nf::Vec3> tessellate_curve(const nf::NurbsCurve& curve,
                                                     const TessParams& params);

/// Appends the triangles of a (trimmed) face to `out`. If `boundary` is true, the face
/// boundaries are added to out.edges (standalone faces; shells draw their shared edges instead).
/// Iso-curves are added to out.isocurves.
void tessellate_face(const nf::TrimmedSurface& face, const TessParams& params, DisplayMesh& out,
                     bool boundary);

/// Complete display data of an entity's geometry.
[[nodiscard]] DisplayMesh tessellate(const document::Geometry& g, const TessParams& params);

}  // namespace nfcad::tessellation
